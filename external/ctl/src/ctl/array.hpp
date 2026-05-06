#ifndef CTL_ARRAY_HPP
#define CTL_ARRAY_HPP
#include "allocator.hpp"
#include "slice.hpp"
#include "maybe.hpp"

namespace ctl {

    struct Allocator;

    /// @brief A dynamic array implementation (similar to std::vector).
    /// 
    /// Requires a polymorphic allocator on construction.
    /// This container does not throw exceptions; allocation failures are reported via boolean returns or Maybe types.
    ///
    /// @tparam T The type of elements stored.
    template<typename T>
    struct Array {
        /// @brief Minimum capacity (in elements) allocated when the array grows for the first time.
	static inline constexpr const auto MIN_CAPACITY = 16;

        /// @brief The capacity growth factor as a percentage (250 = 2.5x).
	static inline constexpr const auto RESIZE_FACTOR = 250;

        /// @brief Constructs an empty array associated with the given allocator.
	constexpr Array(Allocator& allocator)
            : allocator_{allocator}
	{}

        /// @brief Move constructor. Transfers ownership of the buffer.
	constexpr Array(Array&& other)
            : data_{exchange(other.data_, nullptr)}
            , length_{exchange(other.length_, 0)}
            , capacity_{exchange(other.capacity_, 0)}
            , allocator_{other.allocator_}
	{}

        // Copying is disabled to prevent implicit allocations. Use `copy()` instead.
	constexpr Array(const Array&) = delete;

        ~Array() { drop(); }

	Array& operator=(const Array&) = delete;

        /// @brief Move assignment operator.
        /// Destroys the current content and takes ownership of `other`.
	Array& operator=(Array&& other) {
            return *new (drop(), Nat{}) Array{move(other)};
	}

        /// @brief Resizes the array to contain `length` elements.
        /// 
        /// If `length` is smaller than current, elements are destructed.
        /// If `length` is larger, new elements are default-constructed.
        /// 
        /// @return `true` on success, `false` if memory allocation failed.
	Bool resize(Ulen length) {
            if (length < length_) {
                if constexpr (!TriviallyDestructible<T>) {
                    for (Ulen i = length_; i > length; i--) {
                        data_[i - 1].~T();
                    }
                }
            } else if (length > length_) {
                if (!reserve(length)) {
                    return false;
                }
                for (Ulen i = length_; i < length; i++) {
                    new (data_ + i, Nat{}) T{};
                }
            }
            length_ = length;
            return true;
	}

        /// @brief Reserves memory for at least `length` elements.
        /// 
        /// @return `true` on success, `false` if memory allocation failed.
	Bool reserve(Ulen length) {
            if (length < capacity_) {
                return true;
            }
            Ulen capacity = MIN_CAPACITY;
            while (capacity < length) {
                capacity = (capacity * RESIZE_FACTOR) / 100;
            }

            auto data = allocator_.allocate<T>(capacity, false);
            if (!data) {
                return false;
            }
            for (Ulen i = 0; i < length_; i++) {
                new (data + i, Nat{}) T{move(data_[i])};
            }
            drop();
            data_ = data;
            capacity_ = capacity;
            return true;
	}

        /// @brief Constructs an element in-place at the end of the array.
        /// @return `true` on success, `false` on allocation failure.
	template<typename... Ts>
	Bool emplace_back(Ts&&... args) {
            if (!reserve(length_ + 1)) return false;
            new (data_ + length_, Nat{}) T{forward<Ts>(args)...};
            length_++;
            return true;
	}

        /// @brief Appends an element by moving it to the end.
        /// @return `true` on success, `false` on allocation failure.
	Bool push_back(T&& value)
            requires MoveConstructible<T>
	{
            if (!reserve(length_ + 1)) return false;
            new (data_ + length_, Nat{}) T{move(value)};
            length_++;
            return true;
	}

        /// @brief Appends an element by copying it to the end.
        /// @return `true` on success, `false` on allocation failure.
	Bool push_back(const T& value)
            requires CopyConstructible<T>
	{
            if (!reserve(length_ + 1)) return false;
            new (data_ + length_, Nat{}) T{value};
            length_++;
            return true;
	}

        /// @brief Creates a deep copy of the array using the provided allocator.
        /// 
        /// Handles both copy-constructible types and types with a `copy()` method.
        /// @return A new Array wrapped in Maybe, or empty if allocation failed.
	Maybe<Array<T>> copy(Allocator& allocator)
            requires CopyConstructible<T> || MaybeCopyable<T>
	{
            // We do not use resize here because that would default construct the T and
            // T may not have a default constructor. Instead we reserve the space then
            // placement new the copy into each slot, remembering to increase the length
            // for each element we add. This length has to be incremented and cannot be
            // folded with one result.length_ = length_ at the end because it's possible
            // that say the first N elements copy successfully but N+1 fails, in this
            // case we return {} and the [result] destructor has to destroy the N copies
            // which is dependent on the length stored in [result.length_].
            Array<T> result{allocator};
            if (!result.reserve(length_)) {
                return {};
            }
            for (Ulen i = 0; i < length_; i++) {
                if constexpr (CopyConstructible<T>) {
                    new (result.data_ + i, Nat{}) T{data_[i]}; // Call the copy constructor
                    result.length_++;
                } else if constexpr (MaybeCopyable<T>) {
                    auto copied = data_[i].copy(allocator);
                    if (!copied) {
                        return {};
                    }
                    new (result.data_ + i, Nat{}) T{move(*copied)};
                    result.length_++;
                }
            }
            return result;
	}

        /// @brief Removes the last element. Undefined behavior if empty.
	void pop_back() {
            if constexpr (!TriviallyDestructible<T>) {
                data_[length_ - 1].~T();
            }
            length_--;
	}

        /// @brief Removes the first element and shifts remaining elements (O(N)).
	void pop_front() {
            if constexpr (!TriviallyDestructible<T>) {
                data_[0].~T();
            }
            for (Ulen i = 1; i < length_; i++) {
                data_[i - 1] = move(data_[i]);
            }
            length_--;
	}

        /// @brief Destroys all elements but keeps the allocated memory capacity.
	void clear() {
            destruct();
            length_ = 0;
	}

        /// @brief Destroys all elements and releases the memory to the allocator.
	void reset() {
            drop();
            length_ = 0;
            capacity_ = 0;
	}

        /// @brief Returns a pointer to the underlying data.
	CTL_FORCEINLINE constexpr T* data() { return data_; }
	CTL_FORCEINLINE constexpr const T* data() const { return data_; }

        /// @brief Returns a reference to the last element.
	CTL_FORCEINLINE constexpr T& last() { return data_[length_ - 1]; }
	CTL_FORCEINLINE constexpr const T& last() const { return data_[length_ - 1]; }

	[[nodiscard]] CTL_FORCEINLINE constexpr auto length() const { return length_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr auto capacity() const { return capacity_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr auto is_empty() const { return length_ == 0; }
	[[nodiscard]] CTL_FORCEINLINE constexpr Allocator& allocator() { return allocator_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr Allocator& allocator() const { return allocator_; }

        /// @brief Unchecked access to element at index.
	[[nodiscard]] CTL_FORCEINLINE constexpr T& operator[](Ulen index) { return data_[index]; }
	[[nodiscard]] CTL_FORCEINLINE constexpr const T& operator[](Ulen index) const { return data_[index]; }

        /// @brief Returns a slice view of the entire array.
	CTL_FORCEINLINE constexpr Slice<T> slice() { return { data_, length_ }; }
	CTL_FORCEINLINE constexpr Slice<const T> slice() const { return { data_, length_ }; }

	// Just enough to make range based for loops work
	[[nodiscard]] CTL_FORCEINLINE constexpr T* begin() { return data_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr const T* begin() const { return data_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr T* end() { return data_ + length_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr const T* end() const { return data_ + length_; }

    private:
        /// @brief Helper to call destructors on all active elements.
	void destruct() {
            if constexpr (!TriviallyDestructible<T>) {
                for (Ulen i = length_; i > 0; i--) {
                    data_[i - 1].~T();
                }
            }
	}

        Array* drop() {
            destruct();
            allocator_.deallocate(data_, capacity_);
            return this;
        }

	T*         data_     = nullptr;
	Ulen       length_   = 0;
	Ulen       capacity_ = 0;
	Allocator& allocator_;
    };

} // namespace ctl

#endif // CTL_ARRAY_HPP
