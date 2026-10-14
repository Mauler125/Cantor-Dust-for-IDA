#include "math/window.h"
#include "cantordust/scalar_types.h"

#include <algorithm>

namespace cantordust
{

u64 Math_ScaledPosition(
    const u64 maximumOffset,
    const u32 position,
    const u32 maximumPosition )
{
    if ( maximumPosition == 0 )
    {
        return 0;
    }

    const u64 clamped = std::min( position, maximumPosition );
    const u64 quotient = maximumOffset / maximumPosition;
    const u64 remainder = maximumOffset % maximumPosition;

    // Divide first to avoid overflowing at high 64-bit addresses.
    return quotient * clamped + remainder * clamped / maximumPosition;
}

u32 Math_PositionForOffset(
    const u64 maximumOffset,
    const u64 offset,
    const u32 maximumPosition )
{
    if ( maximumOffset == 0 || maximumPosition == 0 )
    {
        return 0;
    }

    const u64 target = std::min( offset, maximumOffset );
    u32 first = 0;
    u32 last = maximumPosition;

    // Binary search avoids a 64-bit multiply by the slider range.
    while ( first < last )
    {
        const u32 middle = first + ( last - first + 1 ) / 2;

        if ( Math_ScaledPosition( maximumOffset, middle, maximumPosition ) <= target )
        {
            first = middle;
        }
        else
        {
            last = middle - 1;
        }
    }

    return first;
}

usize Math_OverviewPixelForOffset(
    const u64 totalByteCount,
    const u64 offset,
    const usize pixelCount )
{
    if ( totalByteCount == 0 || pixelCount == 0 )
    {
        return 0;
    }

    const u64 clamped = std::min( offset, totalByteCount - 1 );
    return Math_PositionForOffset(
        totalByteCount, clamped,
        static_cast< u32 >( pixelCount ) );
}

void Math_OverviewPixelSpan(
    const u64 totalByteCount,
    const u64 offset,
    const u64 byteCount,
    const usize pixelCount,
    usize& startPixel,
    usize& endPixel )
{
    startPixel = 0;
    endPixel = 0;

    if ( totalByteCount == 0 || byteCount == 0 || pixelCount == 0 ||
         offset >= totalByteCount )
    {
        return;
    }

    const u64 clampedByteCount = std::min(
        byteCount, totalByteCount - offset );

    u32 startLow = 0;
    u32 startHigh = static_cast< u32 >( pixelCount );

    while ( startLow < startHigh )
    {
        const u32 middle = startLow + ( startHigh - startLow ) / 2;

        if ( Math_ScaledPosition(
                totalByteCount, middle,
                static_cast< u32 >( pixelCount ) ) < offset )
        {
            startLow = middle + 1;
        }
        else
        {
            startHigh = middle;
        }
    }

    startPixel = totalByteCount >= pixelCount
        ? Math_OverviewPixelForOffset( totalByteCount, offset, pixelCount )
        : startLow;

    const u64 endOffsetExclusive = offset + clampedByteCount;
    u32 low = 0;
    u32 high = static_cast< u32 >( pixelCount );

    while ( low < high )
    {
        const u32 middle = low + ( high - low ) / 2;

        if ( Math_ScaledPosition(
                totalByteCount, middle,
                static_cast< u32 >( pixelCount ) ) <
             endOffsetExclusive )
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }

    endPixel = low;

    if ( endPixel <= startPixel )
    {
        endPixel = std::min( startPixel + 1, pixelCount );
    }
}

} // namespace cantordust
