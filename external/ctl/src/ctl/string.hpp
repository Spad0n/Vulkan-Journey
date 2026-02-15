#ifndef CTL_STRING_HPP
#define CTL_STRING_HPP
#include "array.hpp"
#include "maybe.hpp"
#include "unicode.hpp"

namespace ctl {

    /// @brief A read-only view over a sequence of characters.
    using StringView = Slice<const char>;

    /// @brief Utility for efficiently building strings incrementally.
    struct StringBuilder {
        /// @brief Constructs a builder using the specified allocator.
	constexpr StringBuilder(Allocator& allocator)
            : build_{allocator}
	{}

        /// @brief Appends a single character.
	void put(char ch);
        /// @brief Appends a Unicode rune (encoded as UTF-8).
        void put(Rune r);
        /// @brief Appends a string view.
	void put(StringView view);

	CTL_FORCEINLINE void put(Float32 v) { put(Float64(v)); }
        /// @brief Appends a floating point number (formatted).
	void put(Float64 v);

        // Integer overloads
	CTL_FORCEINLINE void put(Uint8 v) { put(Uint16(v)); }
	CTL_FORCEINLINE void put(Uint16 v) { put(Uint32(v)); }
	CTL_FORCEINLINE void put(Uint32 v) { put(Uint64(v)); }

        /// @brief Appends an unsigned 64-bit integer.
	void put(Uint64 v);

        /// @brief Appends a signed 64-bit integer.
	void put(Sint64 v);

	CTL_FORCEINLINE void put(Sint32 v) { put(Sint64(v)); }
	CTL_FORCEINLINE void put(Sint16 v) { put(Sint32(v)); }
	CTL_FORCEINLINE void put(Sint8 v) { put(Sint16(v)); }

        /// @brief Appends `n` repetitions of character `ch`.
	void rep(Ulen n, char ch = ' ');

        /// @brief Left-pads a character to width `n`.
	void lpad(Ulen n, char ch, char pad = ' ');

        /// @brief Left-pads a string view to width `n`.
	void lpad(Ulen n, StringView view, char pad = ' ');

        /// @brief Right-pads a character to width `n`.
	void rpad(Ulen n, char ch, char pad = ' ');

        /// @brief Right-pads a string view to width `n`.
	void rpad(Ulen n, StringView view, char pad = ' ');

        /// @brief Resets the builder to empty state without freeing memory.
	void reset();

        /// @brief Returns the built string as a view.
        /// @return The result view, or empty if an allocation error occurred during building.
	Maybe<StringView> result() const;

        /// @brief Returns the last inserted string token.
	StringView last() const { return last_; } // Last inserted string token

        /// @brief Frees the underlying memory.
        void destroy();

        /// @brief Clears content (alias for reset).
        void clear();
    private:
	Array<char> build_;
	Bool        error_ = false;
	StringView  last_;
    };

    /// @brief Represents a reference to a string (offset + length).
    /// Typically used in string tables or intern pools.
    struct StringRef {
	constexpr StringRef() = default;
	constexpr StringRef(Unit) : StringRef{}{}
	constexpr StringRef(Uint32 offset, Uint32 length)
            : offset{offset}
            , length{length}
	{}

	Uint32 offset = 0;
	Uint32 length = ~0_u32;

        /// @brief Checks if the reference is valid (length != ~0).
	CTL_FORCEINLINE constexpr auto is_valid() const {
            return length != ~0_u32;
	}
	CTL_FORCEINLINE constexpr operator Bool() const {
            return is_valid();
	}
    };

} // namespace ctl

#endif // CTL_STRING_HPP
