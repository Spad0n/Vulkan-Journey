#define STB_SPRINTF_STATIC
#define STB_SPRINTF_IMPLEMENTATION
#include "third_party/stb_sprintf.h"
#include <stdarg.h>

#include "ctl/string.hpp"
#include "ctl/stream.hpp"

namespace ctl {

    void StringBuilder::put(char ch) {
        if (!build_.push_back(ch)) {
            error_ = true;
            return;
        }
        last_ = { &build_.last(), 1 };
    }

    void StringBuilder::put(Rune r) {
        Uint8 buffer[4];
        Slice<Uint8> dest(buffer, 4);
        const Ulen n = r.encode_utf8(dest);
        if (n == 0) {
            error_ = true;
            return;
        }

        const auto offset = build_.length();
        if (!build_.resize(offset + n)) {
            error_ = true;
            return;
        }
        for (Ulen i = 0; i < n; i++) {
            build_[offset + i] = static_cast<char>(buffer[i]);
        }
        last_ = { &build_[offset], n };
    }

    void StringBuilder::put(StringView view) {
        const auto offset = build_.length();
        const auto len = view.length();
        if (!build_.resize(offset + len)) {
            error_ = true;
            return;
        }
        for (Ulen i = 0; i < len; i++) {
            build_[offset + i] = view[i];
        }
        last_ = { &build_[offset], len };
    }

    void StringBuilder::put(Float64 value) {
	//char buffer[DBL_MANT_DIG + DBL_DECIMAL_DIG * 2 + 1];
        char buffer[STB_SPRINTF_MIN];
	auto n = stbsp_snprintf(buffer, sizeof buffer, "%g", value);
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

	Ulen length = 0;
	for (Uint64 v = value; v; v /= 10) length++;

	Ulen offset = build_.length();
	if (!build_.resize(offset + length)) {
            error_ = true;
            return;
	}

	char *const fill = build_.data() + offset;
        Ulen i = length;
	for (; value; value /= 10) {
            fill[--i] = char('0' + (value % 10));
	}
        last_ = { fill, length };
    }

    void StringBuilder::put(Sint64 value) {
	if (value < 0) {
            const auto offset = build_.length();
            if (!build_.push_back('-')) {
                error_ = true;
                return;
            }
            put(Uint64(-value));
            if (!error_) {
                last_ = { build_.data() + offset, build_.length() - offset };
            }
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
	if (n >= l) rep(n - l, pad);
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

    void StringBuilder::clear() {
        build_.clear();
    }

    struct FmtCtx {
        StringBuilder* sb;
        char           pad[STB_SPRINTF_MIN];
    };

    static char *fmt_callback(const char *buf, void *user, int len) {
        auto *ctx = static_cast<FmtCtx*>(user);
        ctx->sb->put(StringView{ buf, Ulen(len) });
        return ctx->pad;
    }

    Bool StringBuilder::vformat(const char *fmt, void *va_ptr) {
        FmtCtx ctx{ this, {} };
        va_list& va = *reinterpret_cast<va_list*>(va_ptr);
        const auto n = stbsp_vsprintfcb(&fmt_callback, &ctx, ctx.pad, fmt, va);
        return n >= 0 && !error_;
    }

    Bool StringBuilder::format(const char *fmt, ...) {
        va_list va;
        va_start(va, fmt);
        const auto ok = vformat(fmt, &va);
        va_end(va);
        return ok;
    }
} // namespace ctl
