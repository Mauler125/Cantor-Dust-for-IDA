#ifndef MATH_UNIVERSE_H
#define MATH_UNIVERSE_H


#include "cantordust/scalar_types.h"
#include <cstddef>
#include <cstdint>

namespace cantordust
{

struct UniversePosition_s
{
    f32 x = 0;
    f32 y = 0;
    f32 z = 0;
};

UniversePosition_s Math_UniversePosition(
    usize rank, usize total, u64 address );

UniversePosition_s Math_UniverseFocusPosition(
    usize index, usize count, u64 address,
    f32 radius );

u64 Math_UniverseAddressGap(
    u64 address, u64 centerAddress );

f32 Math_UniverseFocusDistance(
    u64 gap, u64 minimumGap,
    u64 maximumGap );

UniversePosition_s Math_UniverseDirection( u64 seed );
UniversePosition_s Math_UniverseAdd(
    const UniversePosition_s& a, const UniversePosition_s& b );

UniversePosition_s Math_UniverseScale(
    const UniversePosition_s& v, f32 scale );

f32 Math_UniverseLength( const UniversePosition_s& v );
UniversePosition_s Math_UniverseNormalize( const UniversePosition_s& v );
UniversePosition_s Math_UniverseCross(
    const UniversePosition_s& a, const UniversePosition_s& b );

bool Math_UniverseClipLine( f32 ax, f32 ay, f32 bx, f32 by,
    f32 width, f32 height, f32& firstT, f32& lastT );

f32 Math_UniverseHeat( u32 references,
    u32 maximumReferences );

f32 Math_UniverseProminence( u32 incoming,
    u32 outgoing );

f32 Math_UniverseFocusCallerRadius( u32 callCount );

f32 Math_UniverseNodeRadius( u32 incoming,
    u32 outgoing );

f32 Math_UniverseLinkStrength( u32 count,
    u32 callerOutgoing,
    u32 calleeIncoming );

} // namespace cantordust

#endif // MATH_UNIVERSE_H
