#ifndef CTL_UNICODE_HPP
#define CTL_UNICODE_HPP
#include "types.hpp"
#include "slice.hpp"

namespace ctl {

    /// @brief Represents a Unicode codepoint (Rune).
    struct Rune {
	constexpr Rune(Uint32 v) : v_{v} {}

        /// @brief Checks if the rune is a valid character or underscore.
	[[nodiscard]] Bool is_char() const;

        /// @brief Checks if the rune is a decimal digit (0-9).
	[[nodiscard]] Bool is_digit() const;

        /// @brief Checks if the rune is a digit in the specified base (up to 36).
	[[nodiscard]] Bool is_digit(Uint32 base) const;

        /// @brief Checks if the rune is alphanumeric.
	[[nodiscard]] Bool is_alpha() const;

        /// @brief Checks if the rune is whitespace (space, tab, newline, return).
	[[nodiscard]] Bool is_white() const;
    
        /// @brief Encodes the rune into UTF-8 bytes.
        /// @param dest The buffer to write to (must be at least 4 bytes).
        /// @return The number of bytes written (0 on error).
        Ulen encode_utf8(Slice<Uint8> dest) const;

        /// @brief Decodes a single rune from a UTF-8 source.
        /// @param src The source buffer.
        /// @param rune [out] The decoded rune.
        /// @return The number of bytes consumed from source (0 on error or if src is too short).
        [[nodiscard]] static Ulen decode_utf8(Slice<const Uint8> src, Rune& rune);

	operator Uint32() const { return v_; }
    private:
	Uint32 v_;
    };

} // namespace ctl

#endif // CTL_UNICODE
