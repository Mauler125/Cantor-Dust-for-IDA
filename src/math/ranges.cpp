#include "math/ranges.h"
#include "cantordust/scalar_types.h"

#include <algorithm>
#include <limits>

namespace cantordust
{

bool Math_AddRange(
    std::vector< ByteRange_s >& ranges,
    const u64 address,
    const u64 length )
{
    if ( length == 0 || length >
         std::numeric_limits< u64 >::max() - address )
    {
        return false;
    }

    const u64 offset = Math_RangeLength( ranges );

    if ( length > std::numeric_limits< u64 >::max() - offset )
    {
        return false;
    }

    ranges.push_back({ address, length, offset });
    return true;
}

u64 Math_RangeLength( const std::vector< ByteRange_s >& ranges )
{
    return ranges.empty() ? 0 : ranges.back().offset + ranges.back().length;
}

bool Math_OffsetToAddress(
    const std::vector< ByteRange_s >& ranges,
    const u64 offset,
    u64& address )
{
    const auto it = std::upper_bound(
        ranges.begin(), ranges.end(), offset,
        []( const u64 value, const ByteRange_s& range ) {
            return value < range.offset;
        } );

    if ( it == ranges.begin() )
    {
        return false;
    }

    const ByteRange_s& range = *( it - 1 );

    if ( offset - range.offset >= range.length )
    {
        return false;
    }

    address = range.address + offset - range.offset;
    return true;
}

bool Math_AddressToOffset(
    const std::vector< ByteRange_s >& ranges,
    const u64 address,
    u64& offset )
{
    // The vector is in IDA's ascending segment order.
    const auto it = std::upper_bound(
        ranges.begin(), ranges.end(), address,
        []( const u64 value, const ByteRange_s& range ) {
            return value < range.address;
        } );

    if ( it == ranges.begin() )
    {
        return false;
    }

    const ByteRange_s& range = *( it - 1 );

    if ( address - range.address >= range.length )
    {
        return false;
    }

    offset = range.offset + address - range.address;
    return true;
}

} // namespace cantordust
