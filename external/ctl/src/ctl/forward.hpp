#ifndef CTL_FORWARD_HPP
#define CTL_FORWARD_HPP
#include "traits.hpp"

namespace ctl {

    /// @brief Forwards an lvalue as either an lvalue or an rvalue, depending on T.
    ///
    /// This is used for "perfect forwarding" to preserve the value category 
    /// (lvalue vs rvalue) of arguments passed to a function template.
    ///
    /// @tparam T The type of the argument being forwarded.
    /// @param arg The argument to forward.
    /// @return The argument cast to `T&&`.
    template<typename T>
    constexpr T&& forward(RemoveReference<T>&& arg) {
	return static_cast<T&&>(arg);
    }

    /// @brief Forwards an lvalue as either an lvalue or an rvalue, depending on T.
    ///
    /// @overload
    template<typename T>
    constexpr T&& forward(RemoveReference<T>& arg) {
	return static_cast<T&&>(arg);
    }

} // namespace ctl

#endif // CTL_FORWARD_HPP
