#include "ctl/info.hpp"
#include "ctl/system.hpp"
#include "ctl/file.hpp"


extern "C" {

    CTL_WASM_IMPORT(env, console_log)
    void host_console_log(const char* ptr, unsigned len);

    CTL_WASM_IMPORT(env, fs_open)
    int host_fs_open(const char* name_ptr, unsigned name_len, unsigned access);

    CTL_WASM_IMPORT(env, fs_close)
    void host_fs_close(int fd);

    CTL_WASM_IMPORT(env, fs_read)
    unsigned host_fs_read(int fd,
                          unsigned long long offset,
                          unsigned char* buf,
                          unsigned len);

    CTL_WASM_IMPORT(env, fs_write)
    unsigned host_fs_write(int fd,
                           unsigned long long offset,
                           const unsigned char* buf,
                           unsigned len);

    CTL_WASM_IMPORT(env, fs_size)
    unsigned long long host_fs_size(int fd);

    CTL_WASM_IMPORT(env, fs_opendir)
    int host_fs_opendir(const char* name_ptr, unsigned name_len);

    CTL_WASM_IMPORT(env, fs_closedir)
    void host_fs_closedir(int dir);

    CTL_WASM_IMPORT(env, fs_readdir)
    unsigned host_fs_readdir(int dir,
                             char* name_out, unsigned name_cap,
                             unsigned* kind_out);

} // extern "C"

namespace ctl {

    // ----------------------------------------------------------------------
    // Heap : Bump allocator bump based on memory.grow.
    //
    // Free is a no-op : on wasm32 without libc we cannot free one
    // individual page (memory.grow can only push water mark
    // mark). It sufficient because CTL use already
    // TemporaryAllocator/ArenaAllocator/ScratchAllocator for
    // controlling memory managment.
    //
    // The symbol __heap_base is submit by wasm-ld
    // ----------------------------------------------------------------------
    extern "C" unsigned char __heap_base;

    static constexpr Ulen WASM_PAGE_SIZE = 65536;

    static unsigned char* g_brk     = nullptr;
    static unsigned char* g_brk_end = nullptr;

    static void heap_init() {
        g_brk = &__heap_base;
        auto base = reinterpret_cast<Address>(g_brk);
        base = (base + 15) & ~Address(15);
        g_brk = reinterpret_cast<unsigned char*>(base);
        const auto pages = __builtin_wasm_memory_size(0);
        g_brk_end = reinterpret_cast<unsigned char*>(pages * WASM_PAGE_SIZE);
    }

    void* Heap::allocate(Ulen length, Bool zero) {
        if (!g_brk) heap_init();

        const Ulen aligned = (length + 15) & ~Ulen(15);

        if (g_brk + aligned > g_brk_end) {
            const Ulen needed = (g_brk + aligned) - g_brk_end;
            const Ulen pages  = (needed + WASM_PAGE_SIZE - 1) / WASM_PAGE_SIZE;
            const auto prev   = __builtin_wasm_memory_grow(0, pages);
            if (prev == static_cast<decltype(prev)>(-1)) {
                return nullptr;
            }
            g_brk_end += pages * WASM_PAGE_SIZE;
        }

        void* result = g_brk;
        g_brk += aligned;

        if (zero) {
            auto p = static_cast<unsigned char*>(result);
            for (Ulen i = 0; i < length; i++) p[i] = 0;
        }
        return result;
    }

    void Heap::deallocate(void* /*addr*/, Ulen /*length*/) {
        // No-op. See comments
    }

    // ----------------------------------------------------------------------
    // Console
    // ----------------------------------------------------------------------
    void Console::print(StringView data) {
        host_console_log(data.data(), static_cast<unsigned>(data.length()));
    }

    // ----------------------------------------------------------------------
    // Filesystem
    // ----------------------------------------------------------------------
    Filesystem::File* Filesystem::open_file(StringView name, Filesystem::Access access) {
        const int fd = host_fs_open(name.data(),
                                    static_cast<unsigned>(name.length()),
                                    static_cast<unsigned>(access));
        if (fd < 0) return nullptr;
        // We encode fd on the pointer. +1 mean fd==0 being not nullptr.
        return reinterpret_cast<Filesystem::File*>(static_cast<Address>(fd + 1));
    }

    void Filesystem::close_file(Filesystem::File* file) {
        if (!file) return;
        const int fd = static_cast<int>(reinterpret_cast<Address>(file)) - 1;
        host_fs_close(fd);
    }

    Uint64 Filesystem::read_file(Filesystem::File* file, Uint64 offset, Slice<Uint8> data) {
        if (!file) return 0;
        const int fd = static_cast<int>(reinterpret_cast<Address>(file)) - 1;
        // wasm32 : len is limited on 32 bits, but Slice can be bigger
        // In théorie. Else we loop.
        Uint64 total = 0;
        while (total < data.length()) {
            const auto chunk = data.length() - total;
            const unsigned want = chunk > 0xffffffffu ? 0xffffffffu
                                                      : static_cast<unsigned>(chunk);
            const auto got = host_fs_read(fd, offset + total, data.data() + total, want);
            if (got == 0) break;
            total += got;
        }
        return total;
    }

    Uint64 Filesystem::write_file(Filesystem::File* file, Uint64 offset, Slice<const Uint8> data) {
        if (!file) return 0;
        const int fd = static_cast<int>(reinterpret_cast<Address>(file)) - 1;
        Uint64 total = 0;
        while (total < data.length()) {
            const auto chunk = data.length() - total;
            const unsigned want = chunk > 0xffffffffu ? 0xffffffffu
                                                      : static_cast<unsigned>(chunk);
            const auto wrote = host_fs_write(fd, offset + total, data.data() + total, want);
            if (wrote == 0) break;
            total += wrote;
        }
        return total;
    }

    Uint64 Filesystem::tell_file(Filesystem::File* file) {
        if (!file) return 0;
        const int fd = static_cast<int>(reinterpret_cast<Address>(file)) - 1;
        return host_fs_size(fd);
    }

    Filesystem::Directory* Filesystem::open_dir(StringView name) {
        const int dir = host_fs_opendir(name.data(),
                                        static_cast<unsigned>(name.length()));
        if (dir < 0) return nullptr;
        return reinterpret_cast<Filesystem::Directory*>(static_cast<Address>(dir + 1));
    }

    void Filesystem::close_dir(Filesystem::Directory* handle) {
        if (!handle) return;
        const int dir = static_cast<int>(reinterpret_cast<Address>(handle)) - 1;
        host_fs_closedir(dir);
    }

    Bool Filesystem::read_dir(Filesystem::Directory* handle, Filesystem::Item& item) {
        if (!handle) return false;
        const int dir = static_cast<int>(reinterpret_cast<Address>(handle)) - 1;

        static char     name_buf[1024];
        unsigned        kind = 0;
        const auto      n    = host_fs_readdir(dir, name_buf, sizeof(name_buf), &kind);
        if (n == 0) return false;

        using Kind = Filesystem::Item::Kind;
        Kind k = Kind::FILE;
        switch (kind) {
        case 0: k = Kind::FILE; break;
        case 1: k = Kind::DIR;  break;
        case 2: k = Kind::LINK; break;
        }
        item = { StringView{ name_buf, n }, k };
        return true;
    }

    // ----------------------------------------------------------------------
    // Linker : pas de chargement dynamique en wasm standalone.
    // Renvoyer null partout est l'option correcte. Si tu veux du late-binding,
    // expose un import JS qui fait du linking via WebAssembly.instantiate.
    // ----------------------------------------------------------------------
    Linker::Library* Linker::load(StringView /*name*/) {
        return nullptr;
    }

    void Linker::close(Linker::Library* /*lib*/) {
        // no-op
    }

    void (*Linker::link(Linker::Library* /*lib*/, const char* /*sym*/))(void) {
        return nullptr;
    }

} // namespace ctl
