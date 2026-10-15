#include "math/raster_fit.h"
#include "math/hilbert.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

using namespace cantordust;

static Image_s MakeRaster( const View_e mode, const usize side )
{
    Image_s result;
    result.width = side;
    result.height = side;
    result.argb.resize( side * side );
    result.sourceIndex.resize( side * side );
    for ( usize ordinal = 0; ordinal < side * side; ++ordinal )
    {
        const Point_s point = mode == CDV_HILBERT
            ? Math_HilbertPosition( ordinal, side )
            : Point_s{ ordinal % side, ordinal / side };
        const usize pixel = point.y * side + point.x;
        result.argb[ pixel ] = static_cast< u32 >( 0xff000000u | ordinal );
        result.sourceIndex[ pixel ] = ordinal;
    }
    return result;
}

static void VerifyFit( const View_e mode,
    const usize side, const int width, const int height )
{
    const Image_s original = MakeRaster( mode, side );
    const FittedRaster_s fitted = Math_FitByteRaster(
        original, mode, width, height );
    const Image_s& image = fitted.image;
    const usize count = side * side;
    const usize pixels = image.width * image.height;

    assert( pixels >= count );
    assert( pixels < count + image.width );
    assert( image.argb.size() == pixels );
    assert( image.sourceIndex.size() == pixels );
    assert( fitted.ordinal.size() == pixels );

    // Every byte sample must remain reachable, even when the rectangle
    // requires a few replicated pixels to fill its final row.
    std::vector< usize > encountered( count, 0 );
    std::vector< Point_s > locations( count );
    for ( usize y = 0; y < image.height; ++y )
    {
        for ( usize x = 0; x < image.width; ++x )
        {
            const usize pixel = y * image.width + x;
            const usize ordinal = fitted.ordinal[ pixel ];
            assert( ordinal < count );
            assert( image.sourceIndex[ pixel ] == ordinal );
            assert( image.argb[ pixel ] == ( 0xff000000u | ordinal ) );
            ++encountered[ ordinal ];
            if ( encountered[ ordinal ] == 1 )
                locations[ ordinal ] = { x, y };
        }
    }
    assert( std::all_of( encountered.begin(), encountered.end(),
        []( const usize occurrences ) { return occurrences > 0; } ) );

    if ( width == height )
    {
        // In a square viewport do not rotate or reorder existing Hilbert maps.
        assert( image.argb == original.argb );
        assert( image.sourceIndex == original.sourceIndex );
    }
    else if ( side >= 16 )
    {
        // Pixels should be close to square at the requested aspect ratio.
        const double target = static_cast< double >( width ) / height;
        const double actual = static_cast< double >( image.width ) / image.height;
        assert( std::abs( target - actual ) / target <
            std::max( 0.05, 2.0 / side ) );
    }

    // A generalized Hilbert traversal keeps adjacent ordinals nearby in
    // every aspect, rather than jumping across an artificially stretched row.
    if ( mode == CDV_HILBERT && width != height )
    {
        for ( usize i = 1; i < count; ++i )
        {
            const auto& a = locations[ i - 1 ];
            const auto& b = locations[ i ];
            const auto dx = std::abs( static_cast< i64 >( a.x ) -
                static_cast< i64 >( b.x ) );
            const auto dy = std::abs( static_cast< i64 >( a.y ) -
                static_cast< i64 >( b.y ) );
            assert( dx <= 2 && dy <= 2 );
        }
    }
}

int main()
{
    for ( const View_e mode : { CDV_LINEAR, CDV_HILBERT } )
    {
        for ( const usize side : { 8u, 16u, 256u, 512u } )
        {
            VerifyFit( mode, side, 500, 500 );
            VerifyFit( mode, side, 720, 360 );
            VerifyFit( mode, side, 300, 750 );
        }
        VerifyFit( mode, 256, 1200, 180 );
        VerifyFit( mode, 256, 180, 1200 );
        VerifyFit( mode, 1024, 1440, 900 );

        // Exercise many thin, tall, odd and nearly-square output rectangles.
        for ( int width = 1; width <= 24; ++width )
            for ( int height = 1; height <= 24; ++height )
                VerifyFit( mode, 8, width, height );
    }

    bool invalid = false;
    try
    {
        (void)Math_FitByteRaster( MakeRaster( CDV_LINEAR, 8 ),
            CDV_DIGRAPH, 100, 100 );
    }
    catch ( const std::invalid_argument& )
    {
        invalid = true;
    }
    assert( invalid );
}
