#include "math/zoom.h"
#include "cantordust/scalar_types.h"

#include "math/window.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#define CANTORDUST_ZOOM_RATIO_STEPS 1000000u
#define CANTORDUST_ZOOM_MAX_WHEEL_STEPS 32

namespace cantordust
{

ZoomRange_s Math_OverviewContainingDetail(
    const u64 totalLength, ZoomRange_s overview, ZoomRange_s detail )
{
    if ( totalLength == 0 )
    {
        return {};
    }

    // Clamp lengths before starts so all interval ends remain <= totalLength,
    // even for large (>4 GiB) logical selections.
    detail.length = std::clamp< u64 >( detail.length, 1u, totalLength );
    detail.start = std::min( detail.start, totalLength - detail.length );
    overview.length = std::clamp< u64 >( overview.length, 1u, totalLength );
    overview.start = std::min( overview.start, totalLength - overview.length );

    const u64 detailEnd = detail.start + detail.length;
    const u64 overviewEnd = overview.start + overview.length;

    if ( detail.start >= overview.start && detailEnd <= overviewEnd )
    {
        return overview;
    }

    // An Overview zoomed into a tiny interval must not remain there when
    // Detail expands to the full segment (or the entire executable).
    const u64 length = std::max( overview.length, detail.length );
    const u64 earliestStart = detailEnd > length
        ? detailEnd - length : 0u;
    const u64 latestStart = std::min( detail.start, totalLength - length );

    return {
        std::clamp( overview.start, earliestStart, latestStart ), length
    };
}

ZoomRange_s Math_WheelZoom(
    const u64 totalLength,
    ZoomRange_s current,
    const u64 anchor,
    const u64 minimumLength,
    const u64 maximumLength,
    const int wheelSteps )
{
    if ( totalLength == 0 )
    {
        return {};
    }

    const u64 maxLength = std::min(
        totalLength, std::max< u64 >( 1, maximumLength ) );

    const u64 minLength = std::min(
        maxLength, std::max< u64 >( 1, minimumLength ) );

    current.length = std::clamp( current.length, minLength, maxLength );
    current.start = std::min( current.start, totalLength - current.length );

    if ( wheelSteps == 0 )
    {
        return current;
    }

    u64 nextLength = current.length;

    const int steps = static_cast< int >( std::min(
        wheelSteps < 0 ? -static_cast< i64 >( wheelSteps )
                       : static_cast< i64 >( wheelSteps ),
        static_cast< i64 >( CANTORDUST_ZOOM_MAX_WHEEL_STEPS ) ) );

    for ( int i = 0; i < steps; ++i )
    {
        if ( wheelSteps > 0 )
        {
            nextLength -= std::max< u64 >( 1, nextLength / 5 );
            nextLength = std::max( nextLength, minLength );
        }
        else
        {
            const u64 increase =
                nextLength / 4 + ( nextLength % 4 != 0 );

            nextLength += std::min( increase, maxLength - nextLength );
        }
    }

    if ( nextLength == current.length )
    {
        return current;
    }

    const u64 clampedAnchor = std::clamp(
        anchor, current.start, current.start + current.length - 1 );

    const u64 localAnchor = clampedAnchor - current.start;

    // Exact for ordinary windows; fixed-point avoids overflow for very
    // large logical address ranges.
    u64 nextLocal = 0;

    if ( localAnchor != 0 && nextLength <=
         std::numeric_limits< u64 >::max() / localAnchor )
    {
        nextLocal = localAnchor * nextLength / current.length;
    }
    else if ( localAnchor != 0 )
    {
        const u32 ratio = Math_PositionForOffset(
            current.length, localAnchor, CANTORDUST_ZOOM_RATIO_STEPS );

        nextLocal = Math_ScaledPosition(
            nextLength, ratio, CANTORDUST_ZOOM_RATIO_STEPS );
    }

    const u64 nextStart = clampedAnchor >= nextLocal
        ? clampedAnchor - nextLocal : 0;

    return {
        std::min( nextStart, totalLength - nextLength ), nextLength
    };
}

} // namespace cantordust
