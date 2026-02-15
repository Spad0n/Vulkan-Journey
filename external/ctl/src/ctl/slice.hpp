#ifndef CTL_SLICE_HPP
#define CTL_SLICE_HPP
#include "types.hpp"
#include "exchange.hpp"
#include "allocator.hpp"

namespace ctl {

    /// @brief A non-owning view over a contiguous block of memory.
    /// 
    /// Equivalent to `std::span` or `std::string_view`. 
    /// `Slice<const char>` is aliased to `StringView`.
    template<typename T>
    struct Slice {
	constexpr Slice() = default;

        /// @brief Constructs a slice from a pointer and a length.
	constexpr Slice(T* data, Ulen length)
            : data_{data}
            , length_{length}
	{}

        /// @brief Constructs a slice from a static array.
	template<Ulen E>
	constexpr Slice(T (&data)[E])
            : data_{data}
            , length_{E}
	{
            if constexpr (is_same<T, const char>) {
                length_--;
            }
	}

        /// @brief Constructs a slice from a C-string (calculates length via strlen).
        /// Only available for Slice<const char>.
        constexpr Slice(const char* cstr) requires (is_same<T, const char>)
            : data_(cstr)
            , length_{0}
        {
            if (data_) {
                while (data_[length_] != '\0') {
                    length_++;
                }
            }
        }

	constexpr Slice(const Slice& other)
            : data_{other.data_}
            , length_{other.length_}
	{}

	constexpr Slice& operator=(const Slice&) = default;
	constexpr Slice(Slice&& other)
            : data_{exchange(other.data_, nullptr)}
            , length_{exchange(other.length_, 0)}
	{}

	[[nodiscard]] CTL_FORCEINLINE constexpr T& operator[](Ulen index) { return data_[index]; }
	[[nodiscard]] CTL_FORCEINLINE constexpr const T& operator[](Ulen index) const { return data_[index]; }

        /// @brief Creates a sub-slice starting at `offset`.
	constexpr Slice slice(Ulen offset) const {
            return Slice{data_ + offset, length_ - offset};
	}

        /// @brief Creates a sub-slice with the first `length` elements.
	constexpr Slice truncate(Ulen length) const {
            return Slice{data_, length};
	}

        /// @brief Creates a sub-slice defined by [start, end_excluded).
        constexpr Slice subrange(Ulen start, Ulen end_excluded) const {
            return slice(start).truncate(end_excluded - start);
        }

	// Just enough to make range based for loops work
	[[nodiscard]] CTL_FORCEINLINE constexpr T* begin() { return data_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr const T* begin() const { return data_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr T* end() { return data_ + length_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr const T* end() const { return data_ + length_; }

	[[nodiscard]] CTL_FORCEINLINE constexpr auto length() const { return length_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr auto is_empty() const { return length_ == 0; }

	[[nodiscard]] CTL_FORCEINLINE constexpr T* data() { return data_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr const T* data() const { return data_; }

        /// @brief Reinterprets the slice data as a slice of type U.
        /// @warning Updates length to match `sizeof(U)`. Ensure alignment and size compatibility.
	template<typename U>
	CTL_FORCEINLINE Slice<U> cast() {
            const auto ptr = reinterpret_cast<U*>(data_);
            return Slice<U> { ptr, (length_ * sizeof(T)) / sizeof(U) };
	}

        /// @brief Reinterprets the slice data as a slice of type const U.
	template<typename U>
	CTL_FORCEINLINE Slice<const U> cast() const {
            const auto ptr = reinterpret_cast<const U*>(data_);
            return Slice<const U> { ptr, (length_ * sizeof(T)) / sizeof(U) };
	}

	[[nodiscard]] friend constexpr Bool operator==(const Slice& lhs, const Slice& rhs) {
            const auto lhs_len = lhs.length();
            const auto rhs_len = rhs.length();
            if (lhs_len != rhs_len) {
                return false;
            }
            if (lhs.data() == rhs.data()) {
                return true;
            }
            for (Ulen i = 0; i < lhs_len; i++) {
                if (lhs[i] != rhs[i]) {
                    return false;
                }
            }
            return true;
	}

    private:
	T*   data_   = nullptr;
	Ulen length_ = 0;
    };

} // namespace ctl

#endif // CTL_SLICE_HPP
