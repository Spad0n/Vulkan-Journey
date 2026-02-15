#include "ctl/types.hpp"
#include "ctl/info.hpp"
#include "ctl/file.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>

#if defined(CTL_CFG_USE_MALLOC)
#include <stdlib.h>
#endif

namespace ctl {

    static Ulen convert_utf8_to_utf16(Slice<const Uint8> utf8, Slice<Uint16> utf16) {
        Uint32 cp = 0;
        Ulen len = 0;
        auto out = utf16.data();
        for (Ulen i = 0; i < utf8.length(); i++) {
            auto element = &utf8[i];
            auto ch = *element;
            if (ch <= 0x7f) {
                cp = ch;
            } else if (ch <= 0xbf) {
                cp = (cp << 6) | (ch & 0x3f);
            } else if (ch <= 0xdf) {
                cp = ch & 0x1f;
            } else if (ch <= 0xef) {
                cp = ch & 0x0f;
            } else {
                cp = ch & 0x07;
            }
            element++;
            if ((*element & 0xc0) != 0x80 && cp <= 0x10ffff) {
                if (cp > 0xffff) {
                    len += 2;
                    if (out) {
                        *out++ = static_cast<Uint16>(0xd800 | (cp >> 10));
                        *out++ = static_cast<Uint16>(0xdc00 | (cp & 0x03ff));
                    }
                } else if (cp < 0xd800 || cp >= 0xe000) {
                    len += 1;
                    if (out) {
                        *out++ = static_cast<Uint16>(cp);
                    }
                }
            }
        }
        return len;
    }

    static Ulen convert_utf16_to_utf8(Slice<const Uint16> utf16, Slice<Uint8> utf8) {
	Uint32 cp = 0;
	Ulen len = 0;
	auto out = utf8.data();
	for (Ulen i = 0; i < utf16.length(); i++) {
            auto element = &utf16[i];
            auto ch = *element;
            if (ch >= 0xd800 && ch <= 0xdbff) {
                cp = ((ch - 0xd800) << 10) | 0x10000;
            } else {
                if (ch >= 0xdc00 && ch <= 0xdfff) {
                    cp |= ch - 0xdc00;
                } else {
                    cp = ch;
                }
                if (cp < 0x7f) {
                    len += 1;
                    if (out) {
                        *out++ = static_cast<Uint8>(cp);
                    }
                } else if (cp < 0x7ff) {
                    len += 2;
                    if (out) {
                        *out++ = static_cast<Uint8>(0xc0 | ((cp >> 6) & 0x1f));
                        *out++ = static_cast<Uint8>(0x80 | (cp & 0x3f));
                    }
                } else if (cp < 0xffff) {
                    len += 3;
                    if (out) {
                        *out++ = static_cast<Uint8>(0xe0 | ((cp >> 12) & 0x0f));
                        *out++ = static_cast<Uint8>(0x80 | ((cp >> 6) & 0x3f));
                        *out++ = static_cast<Uint8>(0x80 | (cp & 0x3f));
                    }
                } else {
                    len += 4;
                    if (out) {
                        *out++ = static_cast<Uint8>(0xf0 | ((cp >> 18) & 0x07));
                        *out++ = static_cast<Uint8>(0x80 | ((cp >> 12) & 0x3f));
                        *out++ = static_cast<Uint8>(0x80 | ((cp >> 6) & 0x3f));
                        *out++ = static_cast<Uint8>(0x80 | (cp & 0x3f));
                    }
                }
                cp = 0;
            }
	}
	return len;
    }

    // We need a mechanism to convert between UTF-8 and UTF-16 here, using only the
    // provided allocator. The rest of this code is untested.
    static Slice<Uint16> utf8_to_utf16(Allocator& allocator, Slice<const Uint8> utf8) {
	const auto len = convert_utf8_to_utf16(utf8, {});
	const auto data = allocator.allocate<Uint16>(len + 1, false);
	if (!data) {
            return {};
	}
	Slice<Uint16> utf16{ data, len };
	convert_utf8_to_utf16(utf8, utf16);
	data[len] = 0;
	return utf16;
    }
    static Slice<Uint8> utf16_to_utf8(Allocator& allocator, Slice<const Uint16> utf16) {
	const auto len = convert_utf16_to_utf8(utf16, {});
	const auto data = allocator.allocate<Uint8>(len + 1, false);
	if (!data) {
            return {};
	}
	Slice<Uint8> utf8{ data, len };
	convert_utf16_to_utf8(utf16, utf8);
	data[len] = 0;
	return utf8;
    }

    Filesystem::File* Filesystem::open_file(StringView name, Filesystem::Access access) {
        SystemAllocator sys;
        ScratchAllocator<1024> scratch{sys};
        auto filename = utf8_to_utf16(scratch, name.cast<const Uint8>());
        if (filename.is_empty()) {
            return nullptr;
        }
        DWORD dwDesiredAccess = 0;
        DWORD dwShareMode = 0;
        DWORD dwCreationDisposition = 0;
        DWORD dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
        switch (access) {
        case Filesystem::Access::RD:
            dwDesiredAccess |= GENERIC_READ;
            dwShareMode |= FILE_SHARE_READ;
            dwCreationDisposition = OPEN_EXISTING;
            break;
        case Filesystem::Access::WR:
            dwDesiredAccess |= GENERIC_WRITE;
            dwShareMode |= FILE_SHARE_WRITE;
            dwCreationDisposition = CREATE_ALWAYS;
            break;
        }
        auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(filename.data()),
                                  dwDesiredAccess,
                                  dwShareMode,
                                  nullptr,
                                  dwCreationDisposition,
                                  dwFlagsAndAttributes,
                                  nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return nullptr;
        }
        return reinterpret_cast<Filesystem::File*>(handle);
    }

    void Filesystem::close_file(Filesystem::File* file) {
	CloseHandle(reinterpret_cast<HANDLE>(file));
    }

    Uint64 Filesystem::read_file(Filesystem::File* file, Uint64 offset, Slice<Uint8> data) {
	OVERLAPPED overlapped{};
	overlapped.OffsetHigh = static_cast<Uint32>((offset & 0xffffffff00000000_u64) >> 32);
	overlapped.Offset     = static_cast<Uint32>((offset & 0x00000000ffffffff_u64));
	DWORD rd = 0;
	auto result = ReadFile(reinterpret_cast<HANDLE>(file),
	                       data.data(),
	                       static_cast<DWORD>(min(data.length(), 0xffffffff_ulen)),
	                       &rd,
	                       &overlapped);
	if (!result && GetLastError() != ERROR_HANDLE_EOF) {
            return 0;
	}
	return static_cast<Uint64>(rd);
    }

    Uint64 Filesystem::write_file(Filesystem::File* file, Uint64 offset, Slice<const Uint8> data) {
	OVERLAPPED overlapped{};
	overlapped.OffsetHigh = static_cast<Uint32>((offset & 0xffffffff00000000_u64) >> 32);
	overlapped.Offset     = static_cast<Uint32>((offset & 0x00000000ffffffff_u64));
	DWORD wr = 0;
	auto result = WriteFile(reinterpret_cast<HANDLE>(file),
                                data.data(),
                                static_cast<DWORD>(min(data.length(), 0xffffffff_ulen)),
                                &wr,
                                &overlapped);
	return static_cast<Uint64>(wr);
    }

    Uint64 Filesystem::tell_file(Filesystem::File* file) {
	auto handle = reinterpret_cast<HANDLE>(file);
	BY_HANDLE_FILE_INFORMATION info;
	if (GetFileInformationByHandle(file, &info)) {
            return (static_cast<Uint64>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
	}
	return 0;
    }

    struct FindData {
	FindData(Allocator& allocator)
            : allocator{allocator}
            , handle{INVALID_HANDLE_VALUE}
	{
	}
	~FindData() {
            if (handle != INVALID_HANDLE_VALUE) {
                FindClose(handle);
            }
	}
	ScratchAllocator<4096> allocator;
	HANDLE                 handle;
	WIN32_FIND_DATAW       data;
    };

    Filesystem::Directory* open_dir(StringView name) {
        SystemAllocator sys;
	auto find = sys.create<FindData>(sys);
	if (!find) {
            return nullptr;
	}
	auto path = utf8_to_utf16(find->allocator, name.cast<const Uint8>());
	if (path.is_empty()) {
            sys.destroy(find);
            return nullptr;
	}
	auto handle = FindFirstFileW(reinterpret_cast<const LPCWSTR>(path.data()),
	                             &find->data);
	if (handle != INVALID_HANDLE_VALUE) {
            find->handle = handle;
            return reinterpret_cast<Filesystem::Directory*>(find);
	}
	sys.destroy(find);
	return nullptr;
    }

    void Filesystem::close_dir(Filesystem::Directory* handle) {
        SystemAllocator sys;
	auto find = reinterpret_cast<FindData*>(handle);
	sys.destroy(find);
    }

    Bool Filesystem::read_dir(Filesystem::Directory* handle, Filesystem::Item& item) {
	auto find = reinterpret_cast<FindData*>(handle);
	// Skip '.' and '..'
	if (find->data.cFileName[0] == L'.' &&
	    find->data.cFileName[1 + !!(find->data.cFileName[1] == L'.')])
            {
		if (!FindNextFileW(find->handle, &find->data)) {
                    return false;
		}
            }
	Slice<const Uint16> utf16 {
            reinterpret_cast<const Uint16*>(find->data.cFileName),
            wcslen(find->data.cFileName)
	};
	auto utf8 = utf16_to_utf8(find->allocator, utf16);
	if (utf8.is_empty()) {
            return false;
	}
	if (find->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            item = { utf8.cast<const char>(), Filesystem::Item::Kind::DIR };
	} else {
            item = { utf8.cast<const char>(), Filesystem::Item::Kind::FILE };
	}
	return true;
    }

    void* Heap::allocate(Ulen length, [[maybe_unused]] Bool zero) {
#if defined(CTL_CFG_USE_MALLOC)
	return zero ? calloc(length, 1) : malloc(length);
#else
	return VirtualAlloc(nullptr, length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#endif
    }

    void Heap::deallocate(void *address, [[maybe_unused]] Ulen length) {
#if defined(CTL_CFG_USE_MALLOC)
	free(address);
#else
	VirtualFree(address, length, MEM_RELEASE);
#endif
    }

    void Console::print(StringView data) {
	auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD bytes_to_write = static_cast<DWORD>(data.length());
	WriteConsoleA(handle, data.data(), bytes_to_write, nullptr, nullptr);
    }

    Linker::Library* linker_load(StringView name) {
	InlineAllocator<1024> buf;
	StringBuilder path{buf};
	path.put(name);
	path.put('.');
	path.put("dll");
	path.put('\0');
	auto result = path.result();
	if (!result) {
            return nullptr;
	}
	if (auto lib = LoadLibraryA(result->data())) {
            return reinterpret_cast<Linker::Library*>(lib);
	}
	return nullptr;
    }

    void Linker::close(Linker::Library* lib) {
	FreeLibrary(reinterpret_cast<HMODULE>(lib));
    }

    void (*Linker::link(Linker::Library* lib, const char* sym))(void) {
	if (auto addr = GetProcAddress(reinterpret_cast<HMODULE>(lib), sym)) {
            return reinterpret_cast<void (*)(void)>(addr);
	}
	return nullptr;
    }
} // namespace Ctl
