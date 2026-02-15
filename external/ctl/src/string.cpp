#include <string.h> // TODO: remove
#include <stdio.h>  // TODO: remove
#include <float.h>

#include "ctl/string.hpp"
#include "ctl/stream.hpp"

namespace ctl {

    // StringBuilder
    void StringBuilder::put(char ch) {
	error_ = !build_.push_back(ch);
	last_ = { &build_.last(), 1 };
    }

    void StringBuilder::put(Rune r) {
        if (!build_.reserve(build_.length() + 4)) {
            error_ = true;
            return;
        }

        Uint8 buffer[4];
        Slice<Uint8> dest(buffer, 4);
        Ulen n = r.encode_utf8(dest);

        for (Ulen i = 0; i < n; i++) {
            build_.push_back(static_cast<char>(buffer[i]));
        }
    }

    void StringBuilder::put(StringView view) {
	const auto offset = build_.length();
	if (!build_.resize(offset + view.length())) {
            error_ = true;
            return;
	}
	const auto len = view.length();
	for (Ulen i = 0; i < len; i++) {
            build_[offset + i] = view[i];
	}
	last_ = { &build_[offset], len };
    }

    void StringBuilder::put(Float64 value) {
	char buffer[DBL_MANT_DIG + DBL_DECIMAL_DIG * 2 + 1];
	auto n = snprintf(buffer, sizeof buffer, "%g", value);
	if (n <= 0) {
            error_ = true;
            return;
	}
	put(StringView { buffer, Ulen(n) });
    }

    void StringBuilder::put(Uint64 value) {
	if (value == 0) {
            return put('0');
	}

	Uint64 length = 0;
	for (Uint64 v = value; v; v /= 10, length++);

	Ulen offset = build_.length();
	if (!build_.resize(offset + length)) {
            error_ = true;
            return;
	}

	char *const fill = build_.data() + offset;
	for (; value; value /= 10) {
            fill[--length] = '0' + (value % 10);
	}
    }

    void StringBuilder::put(Sint64 value) {
	if (value < 0) {
            put('-');
            put(Uint64(-value));
	} else {
            put(Uint64(value));
	}
    }

    void StringBuilder::rep(Ulen n, char ch) {
	for (Ulen i = 0; i < n; i++) put(ch);
    }

    void StringBuilder::lpad(Ulen n, char ch, char pad) {
	if (n) rep(n - 1, pad);
	put(ch);
    }

    void StringBuilder::lpad(Ulen n, StringView view, char pad) {
	const auto l = view.length();
	if (n >= l) rep(n - 1, pad);
	put(view);
    }

    void StringBuilder::rpad(Ulen n, char ch, char pad) {
	put(ch);
	if (n) rep(n - 1, pad);
    }

    void StringBuilder::rpad(Ulen n, StringView view, char pad) {
	const auto l = view.length();
	put(view);
	if (n >= l) rep(n - l, pad);
    }

    void StringBuilder::reset() {
	build_.reset();
	error_ = false;
    }

    Maybe<StringView> StringBuilder::result() const {
	if (error_) {
            return {};
	}
	return StringView { build_.slice() };
    }

    void StringBuilder::destroy() {
        build_.destroy();
    }

    void StringBuilder::clear() {
        build_.clear();
    }
} // namespace ctl
