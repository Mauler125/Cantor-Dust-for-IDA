#ifndef MATH_ZOOM_H
#define MATH_ZOOM_H


#include "cantordust/scalar_types.h"
#include <cstdint>

namespace cantordust
{

struct ZoomRange_s
{
    u64 start = 0;
    u64 length = 0;
};

// Keep the Detail selection visible after its range or position changes.
// Preserve the Overview zoom when it can contain Detail; otherwise expand
// only as much as necessary and pan to the nearest containing viewport.
ZoomRange_s Math_OverviewContainingDetail(
    u64 totalLength, ZoomRange_s overview, ZoomRange_s detail );

ZoomRange_s Math_WheelZoom(
    u64 totalLength,
    ZoomRange_s current,
    u64 anchor,
    u64 minimumLength,
    u64 maximumLength,
    int wheelSteps );

} // namespace cantordust

#endif // MATH_ZOOM_H
