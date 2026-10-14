#include "math/universe.h"
#include "cantordust/scalar_types.h"

#include "cantordust/constants.h"

#include <algorithm>
#include <cmath>

namespace cantordust
{

using std::sin;
using std::cos;
using std::sqrt;

UniversePosition_s Math_UniversePosition(
    const usize rank, const usize total,
    const u64 address )
{
    const f64 ratio = total > 1
        ? static_cast< f64 >( rank ) /
            static_cast< f64 >( total - 1 ) : 0.0;

    const f64 radius = 0.12 + 0.84 * sqrt( ratio );
    const f64 latitude = 1.0 - 2.0 * ( static_cast< f64 >( rank ) + 0.5 ) /
        static_cast< f64 >( std::max< usize >( total, 1 ) );

    const f64 ring = sqrt( std::max( 0.0, 1.0 - latitude * latitude ) );
    const f64 angle = static_cast< f64 >( rank ) * 2.399963229728653 +
        static_cast< f64 >( address % 997u ) * 0.0007;

    return {
        static_cast< f32 >( radius * ring * cos( angle ) ),
        static_cast< f32 >( radius * latitude ),
        static_cast< f32 >( radius * ring * sin( angle ) )
    };
}

u64 Math_UniverseAddressGap(
    const u64 address, const u64 centerAddress )
{
    return address >= centerAddress
        ? address - centerAddress : centerAddress - address;
}

f32 Math_UniverseFocusDistance(
    const u64 gap, const u64 minimumGap,
    const u64 maximumGap )
{
    if ( maximumGap <= minimumGap )
    {
        return 0.85f;
    }

    const u64 bounded = std::clamp( gap, minimumGap, maximumGap );
    const f64 minimum = std::log1p( static_cast< f64 >( minimumGap ) );
    const f64 maximum = std::log1p( static_cast< f64 >( maximumGap ) );
    const f64 value = std::log1p( static_cast< f64 >( bounded ) );
    const f64 proportion = maximum > minimum
        ? ( value - minimum ) / ( maximum - minimum )
        : static_cast< f64 >( bounded - minimumGap ) /
            static_cast< f64 >( maximumGap - minimumGap );

    return 0.66f + 0.50f * static_cast< f32 >(
        std::clamp( proportion, 0.0, 1.0 ) );
}

UniversePosition_s Math_UniverseFocusPosition(
    const usize index, const usize count,
    const u64 address, const f32 radius )
{
    // Keep the Fibonacci directions while moving callers radially.
    const f32 t = static_cast< f32 >( index ) + 0.5f;
    const f32 denominator = static_cast< f32 >(
        std::max< usize >( 1, count ) );

    const f32 y = 1.0f - 2.0f * t / denominator;
    const f32 circle = sqrt( std::max( 0.0f, 1.0f - y * y ) );
    constexpr f32 goldenAngle = 2.39996323f;

    const f32 angle = t * goldenAngle +
        static_cast< f32 >( address % 4093u ) * 0.00041f;

    return { circle * cos( angle ) * radius, y * radius,
        circle * sin( angle ) * radius };
}

UniversePosition_s Math_UniverseDirection( const u64 seed )
{
    const f64 a = static_cast< f64 >( ( seed * 0x9E3779B185EBCA87ull ) & 0xFFFFFFFFull ) /
        static_cast< f64 >( 0xFFFFFFFFull );

    const f64 b = static_cast< f64 >( ( seed * 0xC2B2AE3D27D4EB4Full ) & 0xFFFFFFFFull ) /
        static_cast< f64 >( 0xFFFFFFFFull );

    const f64 z = a * 2.0 - 1.0;
    const f64 ring = sqrt( std::max( 0.0, 1.0 - z * z ) );
    const f64 angle = b * CANTORDUST_TAU_D;
    return {
        static_cast< f32 >( ring * cos( angle ) ),
        static_cast< f32 >( z ),
        static_cast< f32 >( ring * sin( angle ) )
    };
}

UniversePosition_s Math_UniverseAdd(
    const UniversePosition_s& a, const UniversePosition_s& b )
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

UniversePosition_s Math_UniverseScale(
    const UniversePosition_s& v, const f32 scale )
{
    return { v.x * scale, v.y * scale, v.z * scale };
}

f32 Math_UniverseLength( const UniversePosition_s& v )
{
    return sqrt( v.x * v.x + v.y * v.y + v.z * v.z );
}

UniversePosition_s Math_UniverseNormalize( const UniversePosition_s& v )
{
    const f32 length = Math_UniverseLength( v );
    return length <= 0.00001f
        ? UniversePosition_s{ 0.0f, 0.0f, 1.0f }
        : Math_UniverseScale( v, 1.0f / length );
}

UniversePosition_s Math_UniverseCross(
    const UniversePosition_s& a, const UniversePosition_s& b )
{
    return { a.y * b.z - a.z * b.y,
             a.z * b.x - a.x * b.z,
             a.x * b.y - a.y * b.x };
}

bool Math_UniverseClipLine( const f32 ax, const f32 ay,
    const f32 bx, const f32 by, const f32 width, const f32 height,
    f32& firstT, f32& lastT )
{
    firstT = 0.0f;
    lastT = 1.0f;

    const f32 dx = bx - ax;
    const f32 dy = by - ay;
    const f32 p[] = { -dx, dx, -dy, dy };
    const f32 q[] = { ax, width - ax, ay, height - ay };

    for ( int i = 0; i < 4; ++i )
    {
        if ( std::abs( p[ i ] ) < 0.0001f )
        {
            if ( q[ i ] < 0.0f )
                return false;

            continue;
        }

        const f32 t = q[ i ] / p[ i ];

        if ( p[ i ] < 0.0f )
        {
            firstT = std::max( firstT, t );
        }
        else
        {
            lastT = std::min( lastT, t );
        }

        if ( firstT > lastT )
            return false;
    }

    return true;
}

f32 Math_UniverseHeat( const u32 references,
    const u32 maximumReferences )
{
    if ( maximumReferences == 0 || references == 0 )
    {
        return 0.0f;
    }

    const f32 normalized = static_cast< f32 >( references ) /
        static_cast< f32 >( maximumReferences );

    return std::pow( std::clamp( normalized, 0.0f, 1.0f ), 0.85f );
}

f32 Math_UniverseProminence( const u32 incoming,
    const u32 outgoing )
{
    return static_cast< f32 >( incoming ) * 1.85f +
        static_cast< f32 >( outgoing ) * 0.80f;
}

f32 Math_UniverseFocusCallerRadius( const u32 count )
{
    return std::clamp( 0.9f + 0.65f *
        sqrt( static_cast< f32 >( count ) ), 1.1f, 5.0f );
}

f32 Math_UniverseNodeRadius( const u32 incoming,
    const u32 outgoing )
{
    // Emphasize major callees; a handful of references stays near 1 pixel.
    const f32 calls = static_cast< f32 >( incoming );
    const f32 outgoingInfluence = std::min( 0.7f,
        0.12f * sqrt( static_cast< f32 >( outgoing ) ) );

    return std::clamp( 1.05f + sqrt( calls ) * 0.53f +
        outgoingInfluence, 1.05f, 13.0f );
}

f32 Math_UniverseLinkStrength( const u32 count,
    const u32 callerOutgoing,
    const u32 calleeIncoming )
{
    const f64 repeated = std::log2( 1.0 + static_cast< f64 >( count ) );
    const f64 caller = std::log2( 1.0 + static_cast< f64 >( callerOutgoing ) );
    const f64 callee = std::log2( 1.0 + static_cast< f64 >( calleeIncoming ) );
    return static_cast< f32 >( std::clamp(
        0.55 + repeated * 0.60 + callee * 0.55 + caller * 0.18,
        0.55, 8.0 ) );
}

} // namespace cantordust
