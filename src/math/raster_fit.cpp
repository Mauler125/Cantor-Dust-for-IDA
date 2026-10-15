#include "pch/core_pch.h"
#include "math/raster_fit.h"

#include "math/hilbert.h"

#include <cstdlib>
#include <stdexcept>

namespace cantordust
{

namespace
{

// Visit a rectangular generalized Hilbert (Gilbert) curve in order.
// Unlike a stretched square Hilbert image, successive samples occupy
// nearby cells even when the dock is wide or tall. The implementation
// generates only positions, without retaining a second curve buffer.
template< typename Visitor >
void WalkRectangle(
    const i64 x, const i64 y,
    const i64 ax, const i64 ay,
    const i64 bx, const i64 by,
    Visitor& visit )
{
    const i64 width = std::abs( ax + ay );
    const i64 height = std::abs( bx + by );
    const auto direction = []( const i64 v ) -> i64 {
        return ( v > 0 ) - ( v < 0 );
    };
    const i64 dax = direction( ax );
    const i64 day = direction( ay );
    const i64 dbx = direction( bx );
    const i64 dby = direction( by );

    if ( height == 1 )
    {
        for ( i64 i = 0; i < width; ++i )
            visit( x + dax * i, y + day * i );
        return;
    }

    if ( width == 1 )
    {
        for ( i64 i = 0; i < height; ++i )
            visit( x + dbx * i, y + dby * i );
        return;
    }

    i64 halfAx = ax / 2;
    i64 halfAy = ay / 2;
    i64 halfBx = bx / 2;
    i64 halfBy = by / 2;

    if ( 2 * width > 3 * height )
    {
        // For wide rectangles use two long runs, splitting on an odd
        // width when possible to keep the join close to its neighbor.
        if ( std::abs( halfAx + halfAy ) % 2 != 0 && width > 2 )
        {
            halfAx += dax;
            halfAy += day;
        }

        WalkRectangle( x, y, halfAx, halfAy, bx, by, visit );
        WalkRectangle( x + halfAx, y + halfAy,
            ax - halfAx, ay - halfAy, bx, by, visit );
    }
    else
    {
        // Fold three runs to traverse both axes of a general rectangle.
        if ( std::abs( halfBx + halfBy ) % 2 != 0 && height > 2 )
        {
            halfBx += dbx;
            halfBy += dby;
        }

        WalkRectangle( x, y, halfBx, halfBy, halfAx, halfAy, visit );
        WalkRectangle( x + halfBx, y + halfBy,
            ax, ay, bx - halfBx, by - halfBy, visit );
        WalkRectangle( x + ( ax - dax ) + ( halfBx - dbx ),
            y + ( ay - day ) + ( halfBy - dby ),
            -halfBx, -halfBy,
            -( ax - halfAx ), -( ay - halfAy ), visit );
    }
}

} // namespace

FittedRaster_s Math_FitByteRaster(
    const Image_s& source,
    const View_e mode,
    const int viewportWidth,
    const int viewportHeight )
{
    if ( mode != CDV_LINEAR && mode != CDV_HILBERT )
        throw std::invalid_argument( "Only byte rasters can be fitted" );

    if ( source.width == 0 || source.height == 0 ||
         source.width != source.height ||
         source.argb.size() != source.width * source.height ||
         source.sourceIndex.size() != source.argb.size() )
    {
        throw std::invalid_argument( "Invalid source raster" );
    }

    const usize count = source.argb.size();
    const f64 aspect = static_cast< f64 >( std::max( 1, viewportWidth ) ) /
        static_cast< f64 >( std::max( 1, viewportHeight ) );
    const usize width = std::clamp< usize >(
        static_cast< usize >( std::llround( std::sqrt( count * aspect ) ) ),
        1, count );
    const usize height = count / width + ( count % width != 0 );
    const usize pixels = width * height;

    FittedRaster_s result;
    if ( width == source.width && height == source.height )
    {
        // Preserve the original power-of-two Hilbert orientation in square
        // docks. Reflow only when the display aspect ratio needs it.
        result.image = source;
        result.ordinal.resize( count );
        for ( usize ordinal = 0; ordinal < count; ++ordinal )
        {
            const usize pixel = mode == CDV_HILBERT
                ? [&]() {
                    const Point_s point = Math_HilbertPosition( ordinal, source.width );
                    return point.y * source.width + point.x;
                }()
                : ordinal;
            result.ordinal[ pixel ] = ordinal;
        }
        return result;
    }

    result.image.width = width;
    result.image.height = height;
    result.image.argb.resize( pixels );
    result.image.sourceIndex.resize( pixels );
    result.ordinal.resize( pixels );

    // Each source sample is visited at least once. A handful of samples
    // may repeat to fill a rectangle whose area is not exactly count.
    usize step = 0;
    const auto visit = [&]( const i64 x, const i64 y ) {
        const usize ordinal = step * count / pixels;
        const usize sourcePixel = mode == CDV_HILBERT
            ? [&]() {
                const Point_s point = Math_HilbertPosition( ordinal, source.width );
                return point.y * source.width + point.x;
            }()
            : ordinal;
        const usize targetPixel = static_cast< usize >( y ) * width +
            static_cast< usize >( x );

        result.image.argb[ targetPixel ] = source.argb[ sourcePixel ];
        result.image.sourceIndex[ targetPixel ] = source.sourceIndex[ sourcePixel ];
        result.ordinal[ targetPixel ] = ordinal;
        ++step;
    };

    if ( mode == CDV_LINEAR )
    {
        for ( usize y = 0; y < height; ++y )
            for ( usize x = 0; x < width; ++x )
                visit( static_cast< i64 >( x ), static_cast< i64 >( y ) );
    }
    else if ( width >= height )
    {
        WalkRectangle( 0, 0, static_cast< i64 >( width ), 0,
            0, static_cast< i64 >( height ), visit );
    }
    else
    {
        WalkRectangle( 0, 0, 0, static_cast< i64 >( height ),
            static_cast< i64 >( width ), 0, visit );
    }

    return result;
}

} // namespace cantordust
