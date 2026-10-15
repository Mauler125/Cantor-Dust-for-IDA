#include "pch/core_pch.h"
#include "math/hilbert.h"
#include "cantordust/scalar_types.h"

#include <stdexcept>
#include <mutex>

namespace cantordust
{

static void Hilbert_Rotate(
    const usize side,
    usize& x,
    usize& y,
    const usize rx,
    const usize ry )
{
    if ( ry != 0 )
    {
        return;
    }

    if ( rx == 1 )
    {
        x = side - 1 - x;
        y = side - 1 - y;
    }

    std::swap( x, y );
}

Point_s Math_HilbertPosition( const usize index, const usize side )
{
    if ( side == 0 || ( side & ( side - 1 ) ) != 0 ||
         side > 1024 || index >= side * side )
    {
        throw std::out_of_range( "Invalid Hilbert side or index" );
    }

    Point_s point;
    usize remaining = index;

    for ( usize scale = 1; scale < side; scale *= 2 )
    {
        const usize rx = ( remaining / 2 ) & 1;
        const usize ry = ( remaining ^ rx ) & 1;

        Hilbert_Rotate( scale, point.x, point.y, rx, ry );
        point.x += scale * rx;
        point.y += scale * ry;
        remaining /= 4;
    }

    return point;
}

usize Math_HilbertIndex( Point_s point, const usize side )
{
    if ( side == 0 || ( side & ( side - 1 ) ) != 0 ||
         side > 1024 || point.x >= side || point.y >= side )
    {
        throw std::out_of_range( "Invalid Hilbert point" );
    }

    usize index = 0;

    for ( usize scale = side / 2; scale > 0; scale /= 2 )
    {
        const usize rx = ( point.x & scale ) != 0;
        const usize ry = ( point.y & scale ) != 0;
        index += scale * scale * ( ( 3 * rx ) ^ ry );
        Hilbert_Rotate( scale, point.x, point.y, rx, ry );
    }

    return index;
}

const std::vector< u32 >& Math_HilbertPixelOffsets( const usize side )
{
    if ( side == 0 || ( side & ( side - 1 ) ) != 0 || side > 1024 )
        throw std::out_of_range( "Invalid Hilbert side" );

    // The supported powers of two share only eleven small cache descriptors.
    // No pixel storage is allocated until a particular resolution is used.
    static std::array< std::once_flag, 11 > once;
    static std::array< std::vector< u32 >, 11 > offsets;
    unsigned int level = 0;
    for ( usize n = side; n > 1; n >>= 1 )
        ++level;

    std::call_once( once[ level ], [side, level]() {
        std::vector< u32 >& table = offsets[ level ];
        table.resize( side * side );
        for ( usize ordinal = 0; ordinal < table.size(); ++ordinal )
        {
            const Point_s point = Math_HilbertPosition( ordinal, side );
            table[ ordinal ] = static_cast< u32 >( point.y * side + point.x );
        }
    } );
    return offsets[ level ];
}

Point_s Math_HilbertPosition( const usize index )
{
    return Math_HilbertPosition( index, CANTORDUST_SIDE );
}

} // namespace cantordust
