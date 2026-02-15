#ifndef CTL_FILE_HPP
#define CTL_FILE_HPP
#include "array.hpp"
#include "maybe.hpp"
#include "system.hpp"

namespace ctl {

    /// @brief Represents a handle to an open file resource.
    /// 
    /// This class wraps an OS-specific file handle and provides mechanisms 
    /// for random-access reading and writing. It is move-only to ensure unique ownership.
    struct File {
        /// @brief Aliases the filesystem access mode (Read or Write).
	using Access = Filesystem::Access;

        /// @brief Opens a file at the specified path.
        /// 
        /// @param name The file path (UTF-8).
        /// @param access The mode (RD for read-only, WR for write/truncate).
        /// @return A valid File object wrapped in Maybe, or empty on failure.
	static Maybe<File> open(StringView name, Access access);

        /// @brief Move constructor. Transfers ownership of the file handle.
	constexpr File(File&& other)
            : file_{exchange(other.file_, nullptr)}
	{}

	//~File() { close(); }

        /// @brief Move assignment operator. Closes the current file and takes ownership of the other.
	File& operator=(File&& other) {
            return *new (drop(), Nat{}) File{move(other)};
	}

        /// @brief Reads data from the file at a specific offset.
        /// 
        /// This operation is stateless: it does not rely on or modify an internal file cursor.
        /// 
        /// @param offset The absolute byte offset from the beginning of the file.
        /// @param data The destination buffer slice.
        /// @return The number of bytes actually read.
	Uint64 read(Uint64 offset, Slice<Uint8> data) const;

        /// @brief Writes data to the file at a specific offset.
        /// 
        /// This operation is stateless: it does not rely on or modify an internal file cursor.
        /// 
        /// @param offset The absolute byte offset from the beginning of the file.
        /// @param data The source buffer slice containing data to write.
        /// @return The number of bytes actually written.
	[[nodiscard]] Uint64 write(Uint64 offset, Slice<const Uint8> data) const;

        /// @brief Returns the total size of the file.
        /// 
        /// @note Despite the name `tell` (which usually implies cursor position), 
        /// this implementation returns the file size (via fstat/GetFileSize).
	[[nodiscard]] Uint64 tell() const;

        /// @brief Closes the file handle and releases OS resources.
        /// 
        /// This method is idempotent (safe to call multiple times).
	void close();

        /// @brief Reads the entire content of the file into a new array.
        /// 
        /// @param allocator The allocator to use for the returned array.
        /// @return An Array containing the full file content.
	Array<Uint8> map(Allocator& allocator) const;

    private:
	File* drop() {
            close();
            return this;
	}

	constexpr File(Filesystem::File* file)
            : file_{file}
	{}

	Filesystem::File* file_;
    };

} // namespace ctl

#endif // CTL_FILE_HPP
