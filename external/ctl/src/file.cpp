#include "ctl/file.hpp"

namespace ctl {

    Maybe<File> File::open(StringView name, Access access) {
	if (name.is_empty()) {
            return {};
	}
	if (auto file = Filesystem::open_file(name, access)) {
            return File{file};
	}
	return {};
    }

    Uint64 File::read(Uint64 offset, Slice<Uint8> data) const {
	Uint64 total = 0;
	while (total < data.length()) {
            if (auto rd = Filesystem::read_file(file_, offset, data)) {
                total += rd;
                offset += rd;
                data = data.slice(rd);
            } else {
                break;
            }
	}
	return total;
    }

    Uint64 File::write(Uint64 offset, Slice<const Uint8> data) const {
	Uint64 total = 0;
	while (total < data.length()) {
            if (auto wr = Filesystem::write_file(file_, offset, data)) {
                total += wr;
                offset += wr;
                data = data.slice(wr);
            } else {
                break;
            }
	}
	return total;
    }

    Uint64 File::tell() const {
	return Filesystem::tell_file(file_);
    }

    void File::close() {
	if (file_) {
            Filesystem::close_file(file_);
            file_ = nullptr;
	}
    }

    Array<Uint8> File::map(Allocator& allocator) const {
	Array<Uint8> result{allocator};
	if (!result.resize(tell())) {
            return {allocator};
	}
	if (read(0, result.slice()) != result.length()) {
            return {allocator};
	}
	return result;
    }

} // namespace ctl
