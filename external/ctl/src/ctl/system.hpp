#ifndef CTL_SYSTEM_HPP
#define CTL_SYSTEM_HPP
#include "string.hpp"

namespace ctl {
    /// @brief OS-level filesystem and system interactions.
    struct Filesystem {
	struct File;
	struct Directory;

        /// @brief Represents a directory entry.
	struct Item {
            enum class Kind {
                FILE,
                LINK,
                DIR,
            };
            StringView name;
            Kind       kind;
	};

        /// @brief File access mode.
	enum class Access : Uint8 { RD, WR };

        /// @brief Opens a file handle directly via the OS.
	static File* open_file(StringView name, Access access);
	static void close_file(File* file);

        /// @brief Reads from a file handle at a specific offset.
	static Uint64 read_file(File* file, Uint64 offset, Slice<Uint8> data);

        /// @brief Writes to a file handle at a specific offset.
	static Uint64 write_file(File* file, Uint64 offset, Slice<const Uint8> data);

        /// @brief Returns the total size of the file.
	static Uint64 tell_file(File* file);

	Directory* open_dir(StringView name);
	void close_dir(Directory*);

        /// @brief Reads the next item in the directory.
        /// @return true if an item was read, false if end of directory.
	Bool read_dir(Directory*, Item& item);
    };

    /// @brief Low-level memory allocator (wrapper around malloc/mmap/VirtualAlloc).
    struct Heap {
	static void *allocate(Ulen len, Bool zero);
	static void deallocate(void* addr, Ulen len);
    };

    /// @brief Standard output interaction.
    struct Console {
	static void print(StringView data);
    };

    /// @brief Dynamic library loader.
    struct Linker {
	struct Library;

        /// @brief Loads a dynamic library (.so/.dll).
	Library* load(StringView name);
	void close(Library* library);

        /// @brief Resolves a symbol address from the library.
        void (*link(Linker::Library* lib, const char* symbol))(void);
    };
} // namespace ctl

#endif // CTL_SYSTEM_HPP
