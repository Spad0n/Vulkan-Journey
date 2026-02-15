#include "ctl/info.hpp"

// Implementation of the System for POSIX systems
#include <sys/stat.h> // fstat, struct stat
#include <sys/mman.h> // PROT_READ, PROT_WRITE, MAP_PRIVATE, MAP_ANONYMOUS
#include <unistd.h> // open, close, pread, pwrite
#include <fcntl.h> // O_CLOEXEC, O_RDONLY, O_WRONLY
#include <dirent.h> // opendir, readdir, closedir
#include <string.h> // strlen, memcpy
#include <dlfcn.h> // dlopen, dlclose, dlsym, RTLD_NOW
#include <stdlib.h> // exit needed from libc since it calls destructors

#include "ctl/system.hpp"

namespace ctl {

    Filesystem::File* Filesystem::open_file(StringView name, Filesystem::Access access) {
	int flags = O_CLOEXEC;
	switch (access) {
	case Filesystem::Access::RD:
            flags |= O_RDONLY;
            break;
	case Filesystem::Access::WR:
            flags |= O_WRONLY;
            flags |= O_CREAT;
            flags |= O_TRUNC;
            break;
	}
        SystemAllocator sys_allocator;
	ScratchAllocator<1024> scratch{sys_allocator};
	auto path = scratch.allocate<char>(name.length() + 1, false);
	if (!path) {
            // Out of memory.
            return nullptr;
	}
	memcpy(path, name.data(), name.length());
	path[name.length()] = '\0';
	auto fd = open(path, flags, 0666);
	if (fd < 0) {
            return nullptr;
	}
	return reinterpret_cast<Filesystem::File*>(fd);
    }

    void Filesystem::close_file(Filesystem::File* file) {
	close(reinterpret_cast<Address>(file));
    }

    Uint64 Filesystem::read_file(Filesystem::File* file, Uint64 offset, Slice<Uint8> data) {
	auto fd = reinterpret_cast<Address>(file);
	auto v = pread(fd, data.data(), data.length(), offset);
	if (v < 0) {
            return 0;
	}
	return Uint64(v);
    }

    Uint64 Filesystem::write_file(Filesystem::File* file, Uint64 offset, Slice<const Uint8> data) {
	auto fd = reinterpret_cast<Address>(file);
	auto v = pwrite(fd, data.data(), data.length(), offset);
	if (v < 0) {
            return 0;
	}
	return Uint64(v);
    }

    Uint64 Filesystem::tell_file(Filesystem::File* file) {
	auto fd = reinterpret_cast<Address>(file);
	struct stat buf;
	if (fstat(fd, &buf) != -1) {
            return Uint64(buf.st_size);
	}
	return 0;
    }

    Filesystem::Directory* Filesystem::open_dir(StringView name) {
        auto sys_allocator = SystemAllocator();
	ScratchAllocator<1024> scratch{sys_allocator};
	auto path = scratch.allocate<char>(name.length() + 1, false);
	if (!path) {
            return nullptr;
	}
	memcpy(path, name.data(), name.length());
	path[name.length()] = '\0';
	auto dir = opendir(path);
	return reinterpret_cast<Filesystem::Directory*>(dir);
    }

    void Filesystem::close_dir(Filesystem::Directory* handle) {
	auto dir = reinterpret_cast<DIR*>(handle);
	closedir(dir);
    }

    Bool Filesystem::read_dir(Filesystem::Directory* handle, Filesystem::Item& item) {
	auto dir = reinterpret_cast<DIR*>(handle);
	auto next = readdir(dir);
	using Kind = Filesystem::Item::Kind;
	while (next) {
            // Skip '.' and '..'
            while (next && next->d_name[0] == '.' && !(next->d_name[1 + (next->d_name[1] == '.')])) {
                next = readdir(dir);
            }
            if (!next) {
                return false;
            }
            // Note: d_type ok on linux
            switch (next->d_type) {
            case DT_LNK:
                item = { StringView { next->d_name, strlen(next->d_name) }, Kind::LINK };
                return true;
            case DT_DIR:
                item = { StringView { next->d_name, strlen(next->d_name) }, Kind::DIR };
                return true;
            case DT_REG:
                item = { StringView { next->d_name, strlen(next->d_name) }, Kind::FILE };
                return true;
            }
            next = readdir(dir);
	}
	return false;
    }

    void* Heap::allocate(Ulen length, [[maybe_unused]] Bool zero) {
#if defined(CTL_CFG_USE_MALLOC)
	return zero ? calloc(length, 1) : malloc(length);
#else
	auto addr = mmap(nullptr,
	                 length,
	                 PROT_WRITE,
	                 MAP_PRIVATE | MAP_ANONYMOUS,
	                 -1,
	                 0);
	if (addr == MAP_FAILED) {
            return nullptr;
	}
	return addr;
#endif
    }

    void Heap::deallocate(void *addr, [[maybe_unused]] Ulen length) {
#if defined(CTL_CFG_USE_MALLOC)
	free(addr);
#else
	munmap(addr, length);
#endif
    }

    void Console::print(StringView data) {
	write(STDOUT_FILENO, data.data(), data.length());
    }

    Linker::Library* Linker::load(StringView name) {
	// Search for the librart next to the executable first.
	InlineAllocator<1024> buf;
	StringBuilder path{buf};
	path.put("./");
	path.put(name);
	path.put('.');
	path.put("so");
	path.put('\0');
	auto result = path.result();
	if (!result) {
            return nullptr;
	}
	if (auto lib = dlopen(result->data(), RTLD_NOW)) {
            return static_cast<Linker::Library*>(lib);
	}
	// Skip the "./" in result to try the system path now.
	if (auto lib = dlopen(result->data() + 2, RTLD_NOW)) {
            return static_cast<Linker::Library*>(lib);
	}
	return nullptr;
    }

    void Linker::close(Linker::Library* lib) {
	dlclose(static_cast<void*>(lib));
    }

    void (*Linker::link(Linker::Library* lib, const char* sym))(void) {
	if (auto addr = dlsym(static_cast<void*>(lib), sym)) {
            return reinterpret_cast<void (*)(void)>(addr);
	}
	return nullptr;
    }
} // namespace ctl

