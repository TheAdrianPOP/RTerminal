#pragma once
#ifndef MOVE_H
#define MOVE_H

// rt_move / rt_forward — drop-in replacements for std::move / std::forward.
// Avoids pulling in <utility>.

// Strip reference qualifiers from a type.
template<typename T> struct RmRef      { using type = T; };
template<typename T> struct RmRef<T&>  { using type = T; };
template<typename T> struct RmRef<T&&> { using type = T; };

template<typename T>
inline constexpr typename RmRef<T>::type&&
rt_move(T&& v) noexcept {
    return static_cast<typename RmRef<T>::type&&>(v);
}

template<typename T>
inline constexpr T&&
rt_forward(typename RmRef<T>::type& v) noexcept {
    return static_cast<T&&>(v);
}

template<typename T>
inline constexpr T&&
rt_forward(typename RmRef<T>::type&& v) noexcept {
    return static_cast<T&&>(v);
}

#endif
