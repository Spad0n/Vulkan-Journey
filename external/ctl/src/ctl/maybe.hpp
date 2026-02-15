#ifndef CTL_MAYBE_H
#define CTL_MAYBE_H
#include "exchange.hpp"
#include "traits.hpp"

namespace ctl {

    struct Allocator;

    template<typename T>
    struct Maybe;

    /// @brief Concept checking if T has a `copy(Allocator&)` method returning `Maybe<T>`.
    /// Used for deep copying complex types like Arrays inside a Maybe.
    template<typename T>
    concept MaybeCopyable = requires(const T& value) {
	{ value.copy(declval<Allocator&>()) } -> Same<Maybe<T>>;
    };

    /// @brief A vocabulary type representing an optional value.
    /// 
    /// Can either hold a value of type T or nothing.
    /// Comparable to `std::optional`.
    ///
    /// @tparam T The type of the value to hold.
    template<typename T>
    struct Maybe {
        /// @brief Constructs an empty Maybe (no value).
	constexpr Maybe()
            : as_nat_{}
	{}

        /// @brief Constructs an empty Maybe using the Unit type.
	constexpr Maybe(Unit)
            : as_nat_{}
	{}

        /// @brief Constructs a Maybe containing a value (moves the value in).
	constexpr Maybe(T&& value)
            : as_value_{move(value)}
            , valid_{true}
	{}

        /// @brief Move constructor. Transfers ownership of the value.
	constexpr Maybe(Maybe&& other)
            : valid_{exchange(other.valid_, false)}
	{
            if (valid_) {
                new (&as_value_, Nat{}) T{move(other.as_value_)};
                if constexpr (!TriviallyDestructible<T>) {
                    other.as_value_.~T();
                }
            }
	}

        /// @brief Assigns a new value, destroying the old one if it existed.
	constexpr Maybe& operator=(T&& value) {
            return *new(drop(), Nat{}) Maybe{move(value)};
	}

        /// @brief Move assignment.
	constexpr Maybe& operator=(Maybe&& value) {
            return *new(drop(), Nat{}) Maybe{move(value)};
	}

        /// @brief Creates a copy of the value using the copy constructor.
        /// @return A new Maybe containing a copy, or empty if this was empty.
	Maybe<T> copy()
            requires CopyConstructible<T>
	{
            if (!is_valid()) {
                return {};
            }
            return Maybe { as_value_ };
	}

        /// @brief Creates a deep copy of the value using a specific allocator.
        /// Useful for container types (e.g. `Maybe<Array<int>>`).
	Maybe<T> copy(Allocator& allocator)
            requires MaybeCopyable<T>
	{
            if (!is_valid()) {
                return {};
            }
            return as_value_.copy(allocator);
	}

        /// @brief Checks if the Maybe contains a value.
	[[nodiscard]] CTL_FORCEINLINE constexpr auto is_valid() const { return valid_; }

        /// @brief Implicit boolean conversion checking validity.
        /// @code if (maybe) { ... } @endcode
	[[nodiscard]] CTL_FORCEINLINE constexpr operator Bool() const { return is_valid(); }

        /// @brief Accesses the contained value. Undefined behavior if empty.
	[[nodiscard]] CTL_FORCEINLINE constexpr T& value() { return as_value_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr const T& value() const { return as_value_; }

        /// @brief Dereferences the contained value.
	[[nodiscard]] CTL_FORCEINLINE constexpr T& operator*() { return as_value_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr const T& operator*() const { return as_value_; }

        /// @brief Accesses members of the contained value.
	[[nodiscard]] CTL_FORCEINLINE constexpr T* operator->() { return &as_value_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr const T* operator->() const { return &as_value_; }

	~Maybe() 
            requires (!TriviallyDestructible<T>)
	{
            drop();
	}

	constexpr ~Maybe() requires TriviallyDestructible<T> = default;

        /// @brief Destroys the contained value (if any) and makes the Maybe empty.
	CTL_FORCEINLINE void reset() {
            drop();
            valid_ = false;
	}

        /// @brief Constructs a new value in-place using the provided arguments.
	template<typename... Ts>
	void emplace(Ts&&... args) {
            new (drop(), Nat{}) Maybe{T{forward<Ts>(args)...}};
	}
    private:
	CTL_FORCEINLINE constexpr Maybe* drop() {
            if constexpr (!TriviallyDestructible<T>) {
                if (is_valid()) as_value_.~T();
            }
            return this;
	}
	union {
            Nat as_nat_;
            T   as_value_;
	};
	Bool valid_ = false;
    };

} // namespace ctl

#endif // CTL_MAYBE_H
