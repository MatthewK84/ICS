#pragma once

#include <type_traits>

namespace ics {

// An element type the fixed-capacity containers can hold (ICS-015). Every
// slot is default-constructed with its container, and elements move in and out
// by assignment, so none of this may throw or allocate beyond what T itself
// does.
template <typename T>
concept Storable = std::is_nothrow_default_constructible_v<T> && std::is_nothrow_move_constructible_v<T> &&
                   std::is_nothrow_move_assignable_v<T>;

}  // namespace ics
