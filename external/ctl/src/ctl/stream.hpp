#ifndef CTL_STREAM_HPP
#define CTL_STREAM_HPP
#include "file.hpp"

namespace ctl {

    /// @brief Abstract interface for sequential data I/O.
    struct Stream {

        /// @brief Writes data to the stream.
        /// @param data The slice of bytes to write.
        /// @return The number of bytes actually written.
	virtual Ulen write(Slice<const Uint8> data) = 0;

        /// @brief Reads data from the stream into a buffer.
        /// @param data The destination buffer slice.
        /// @return The number of bytes actually read. Returns 0 on EOF or error.
	virtual Ulen read(Slice<Uint8> data) = 0;

        /// @brief Returns the current position in the stream.
        /// @return The byte offset from the beginning.
	virtual Uint64 tell() const = 0;
    };

    /// @brief A concrete Stream implementation backed by a filesystem file.
    struct FileStream : Stream {

        /// @brief Move constructor. Takes ownership of the underlying file handle.
	FileStream(FileStream&& other)
            : Stream{move(other)}
            , file_{move(other.file_)}
            , offset_{exchange(other.offset_, 0)}
	{}

        /// @brief Opens a file and returns a stream wrapper around it.
        /// @param name The path to the file.
        /// @param access The access mode (Read or Write).
        /// @return A valid FileStream on success, or empty on failure.
	static Maybe<FileStream> open(StringView name, File::Access access);

        /// @brief Closes the underlying file handle.
        void close();

	virtual Ulen write(Slice<const Uint8> data);
	virtual Ulen read(Slice<Uint8> data);
	virtual Uint64 tell() const;
    private:
	FileStream(File&& file)
            : file_{move(file)}
	{}
	File   file_;
	Uint64 offset_ = 0;
    };

} // namespace ctl

#endif // CTL_STREAM_HPP
