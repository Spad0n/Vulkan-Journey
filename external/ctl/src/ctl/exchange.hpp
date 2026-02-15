#ifndef CTL_EXCHANGE_H
#define CTL_EXCHANGE_H
#include "move.hpp"
#include "forward.hpp"

namespace ctl {

    /// @brief Replaces the value of an object with a new one and returns the old value.
    ///
    /// This function moves the current value of `obj` into the return value,
    /// and then assigns `new_value` to `obj`.
    ///
    /// @tparam T The type of the object being modified.
    /// @tparam U The type of the new value (deduced).
    /// @param obj The object to be exchanged.
    /// @param new_value The value to assign to `obj`.
    /// @return The old value of `obj`.
    ///
    /// @code
    /// void* ptr = malloc(10);
    /// // Steal the pointer and reset the original to nullptr atomically (logic-wise)
    /// void* old_ptr = ctl::exchange(ptr, nullptr);
    /// free(old_ptr);
    /// @endcode
    template<typename T, typename U = T>
    CTL_FORCEINLINE constexpr T exchange(T& obj, U&& new_value) {
	T old_value = move(obj);
	obj = forward<U>(new_value);
	return old_value;
    }

} // namespace Ctl

#endif // CTL_EXCHANGE_H
