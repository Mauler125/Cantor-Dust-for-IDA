#ifndef MATH_RANGES_H
#define MATH_RANGES_H


#include "cantordust/scalar_types.h"
#include <cstdint>
#include <vector>

namespace cantordust
{

struct ByteRange_s
{
    u64 address = 0;
    u64 length = 0;
    u64 offset = 0;
};

bool Math_AddRange(
    std::vector< ByteRange_s >& ranges,
    u64 address, u64 length );

u64 Math_RangeLength( const std::vector< ByteRange_s >& ranges );

bool Math_OffsetToAddress(
    const std::vector< ByteRange_s >& ranges,
    u64 offset, u64& address );

bool Math_AddressToOffset(
    const std::vector< ByteRange_s >& ranges,
    u64 address, u64& offset );

} // namespace cantordust

#endif // MATH_RANGES_H
