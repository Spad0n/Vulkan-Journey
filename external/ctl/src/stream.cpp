#include "ctl/stream.hpp"

namespace ctl {

    Maybe<FileStream> FileStream::open(StringView name, File::Access access) {
	auto file = File::open(name, access);
	if (!file) {
            return {};
	}
	return FileStream { move(*file) };
    }

    void FileStream::close() {
        file_.close();
    }

    Ulen FileStream::write(Slice<const Uint8> data) {
	const auto nb = file_.write(offset_, data);
	offset_ += nb;
        return Ulen(nb);
    }

    Ulen FileStream::read(Slice<Uint8> data) {
	const auto nb = file_.read(offset_, data);
	offset_ += nb;
        return Ulen(nb);
    }

    Uint64 FileStream::tell() const {
	return offset_;
    }

} // namespace ctl
