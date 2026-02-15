#ifndef CTL_MOVE_HPP
#define CTL_MOVE_HPP
#include "traits.hpp"

namespace ctl {

    /// @brief Casts an object to an rvalue reference, enabling move semantics.
    ///
    /// This function does not move anything by itself. It just performs a cast 
    /// that allows the compiler to select move constructors or move assignment operators.
    ///
    /// @param arg The object to be moved.
    /// @return A reference to `arg` treated as an rvalue (xvalue).
    ///
    /// @code
    /// Array<int> a = ...;
    /// Array<int> b = ctl::move(a); // Invokes move constructor
    /// // 'a' is now in a valid but unspecified state.
    /// @endcode
    template<typename T>
    CTL_FORCEINLINE constexpr RemoveReference<T>&& move(T&& arg) {
	return static_cast<RemoveReference<T>&&>(arg);
    }

} // namespace ctl

#endif // CTL_MOVE_HPP
