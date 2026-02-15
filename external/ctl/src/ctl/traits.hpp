#ifndef CTL_TRAITS_HPP
#define CTL_TRAITS_HPP
#include "types.hpp"

namespace ctl {

    template<typename T> struct RemoveReference_      { using Type = T; };
    template<typename T> struct RemoveReference_<T&>  { using Type = T; };
    template<typename T> struct RemoveReference_<T&&> { using Type = T; };

    /// @brief Removes reference from type T (e.g. int& -> int).
    template<typename T>
    using RemoveReference = typename RemoveReference_<T>::Type;

    template<typename T>
    concept Referenceable = requires {
	typename Identity<T&>;
    };

    template<typename T, auto = Referenceable<T>>
    struct AddLValueReference_ { using Type = T; };
    template<typename T, auto = Referenceable<T>>
    struct AddRValueReference_ { using Type = T; };

    template<typename T> struct AddLValueReference_<T, true> { using Type = T&; };
    template<typename T> struct AddRValueReference_<T, true> { using Type = T&&; };

    template<typename T> using AddLValueReference = typename AddLValueReference_<T>::Type;
    template<typename T> using AddRValueReference = typename AddRValueReference_<T>::Type;

    /// @brief Checks if T can be constructed from a const T&.
    template<typename T>
    inline constexpr auto is_copy_constructible =
	__is_constructible(T, AddLValueReference<const T>);

    /// @brief Checks if T can be constructed from a T&&.
    template<typename T>
    inline constexpr auto is_move_constructible =
	__is_constructible(T, AddRValueReference<T>);

    /// @brief Concept ensuring T is copy constructible.
    template<typename T>
    concept CopyConstructible = is_copy_constructible<T>;

    /// @brief Concept ensuring T is move constructible.
    template<typename T>
    concept MoveConstructible = is_move_constructible<T>;

    template<typename T1, typename T2>
    inline constexpr auto is_same = false;

    template<typename T>
    inline constexpr auto is_same<T, T> = true;

    /// @brief Concept ensuring T1 and T2 are exactly the same type.
    template<typename T1, typename T2>
    concept Same = is_same<T1, T2>;

    template<typename B, typename D>
    inline constexpr auto is_base_of = __is_base_of(B, D);

    /// @brief Concept ensuring D derives from B.
    template<typename D, typename B>
    concept DerivedFrom = is_base_of<B, D>;

    template<typename T>
    inline constexpr bool is_polymorphic = __is_polymorphic(T);

    template<typename T>
    inline constexpr bool is_trivially_destructible =
#if CTL_HAS_BUILTIN(__is_trivially_destructible) || defined(CTL_COMPILER_MSVC)
	__is_trivially_destructible(T);
#elif CTL_HAS_BUILTIN(__has_trivial_destructor)
    __has_trivial_destructor(T);
#else
    ([] { static_assert(false, "Cannot implement is_trivially_destructible"); }, false);
#endif

    template<typename T>
    concept HasDestroyMethod = requires(T& t) {
        { t.destroy() };
    };

    /// @brief Concept ensuring T has a trivial destructor (no-op).
    template<typename T>
    concept TriviallyDestructible = is_trivially_destructible<T>;

    /// @brief Utility to obtain a reference to T in unevaluated contexts.
    template<typename T> AddLValueReference<T> declval();

} // namespace ctl

#endif // CTL_TRAITS_HPP
