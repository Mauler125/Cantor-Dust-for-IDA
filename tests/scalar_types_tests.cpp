#include "cantordust/scalar_types.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

using namespace cantordust;

static_assert( std::is_same_v< i8, std::int8_t > );
static_assert( std::is_same_v< u8, std::uint8_t > );
static_assert( std::is_same_v< i16, std::int16_t > );
static_assert( std::is_same_v< u16, std::uint16_t > );
static_assert( std::is_same_v< i32, std::int32_t > );
static_assert( std::is_same_v< u32, std::uint32_t > );
static_assert( std::is_same_v< i64, std::int64_t > );
static_assert( std::is_same_v< u64, std::uint64_t > );
static_assert( std::is_same_v< f32, float > );
static_assert( std::is_same_v< f64, double > );
static_assert( std::is_same_v< usize, std::size_t > );
static_assert( std::is_same_v< isize, std::make_signed_t< usize > > );
static_assert( std::is_same_v< iptr, std::intptr_t > );
static_assert( std::is_same_v< uptr, std::uintptr_t > );
static_assert( std::is_signed_v< i8 > );
static_assert( std::is_unsigned_v< u8 > );
static_assert( std::is_signed_v< isize > );
static_assert( sizeof( iptr ) == sizeof( void* ) );
static_assert( sizeof( uptr ) == sizeof( void* ) );

int main()
{
    return 0;
}
