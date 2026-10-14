#ifndef CANTORDUST_SCALAR_TYPES_H
#define CANTORDUST_SCALAR_TYPES_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cantordust
{

// Explicit-width integers: these aliases are identical to the standard types.
using i8  = std::int8_t;
using u8  = std::uint8_t;
using i16 = std::int16_t;
using u16 = std::uint16_t;
using i32 = std::int32_t;
using u32 = std::uint32_t;
using i64 = std::int64_t;
using u64 = std::uint64_t;

// Required by the target platforms' ABIs (Windows, Linux, macOS, x64, ARM64).
using f32 = float;
using f64 = double;

// Size and pointer-width integer types, independent of 32/64-bit builds.
using usize = std::size_t;
using isize = std::make_signed_t< std::size_t >;
using iptr = std::intptr_t;
using uptr = std::uintptr_t;

// Use isize for signed sizes; leave the SDK / POSIX ssize_t API type alone.
static_assert( sizeof( f32 ) == 4, "CantorDust requires 32-bit floats" );
static_assert( sizeof( f64 ) == 8, "CantorDust requires 64-bit doubles" );

} // namespace cantordust

#endif // CANTORDUST_SCALAR_TYPES_H
