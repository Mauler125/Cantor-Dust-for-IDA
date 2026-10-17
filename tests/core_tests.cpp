#include "cantordust/renderer.h"
#include "cantordust/cube.h"
#include "math/cube.h"

#include "math/color.h"
#include "math/density.h"
#include "math/entropy.h"
#include "math/hilbert.h"
#include "math/pixel.h"
#include "ida/navigation_gate.h"
#include "math/window.h"
#include "math/ranges.h"
#include "math/zoom.h"

#ifdef NDEBUG
    #undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace cantordust;

static void Tests_RenderModes()
{
    const std::vector< ByteCell_s > cells = {
        { 0x41, true }, { 0x42, true }, { 0x00, false },
        { 0x41, true }, { 0x42, true }, { 0xFF, true }
    };

    const Image_s blank;
    assert( blank.SourceAt( 0, 0 ) == CANTORDUST_NO_SOURCE );

    const Image_s empty = Render( CDV_LINEAR, {} );
    assert( empty.SourceAt( 0, 0 ) == CANTORDUST_NO_SOURCE );
    assert( empty.SourceAt( CANTORDUST_SIDE, 0 ) == CANTORDUST_NO_SOURCE );

    const Image_s raster = Render( CDV_LINEAR, cells );
    assert( raster.SourceAt( 0, 0 ) == 0 );
    assert( raster.SourceAt( 1, 0 ) == 1 );
    assert( raster.SourceAt( 2, 0 ) == CANTORDUST_NO_SOURCE );
    assert( raster.SourceAt( 0, 1 ) == CANTORDUST_NO_SOURCE );

    const Image_s digraph = Render( CDV_DIGRAPH, cells );
    assert( digraph.SourceAt( 0x41, 255 - 0x42 ) == 0 );
    assert( digraph.SourceAt( 0x42, 255 - 0x00 ) == CANTORDUST_NO_SOURCE );
    assert( digraph.SourceAt( 0x42, 255 - 0xFF ) == 4 );

    const Image_s histogram = Render( CDV_HISTOGRAM, cells );
    assert( histogram.SourceAt( 0x41, 255 ) == 0 );
    assert( histogram.SourceAt( 0x42, 255 ) == 1 );
    assert( histogram.SourceAt( 0, 255 ) == CANTORDUST_NO_SOURCE );
    assert( histogram.SourceAt( 0xFF, 255 ) == 5 );

    const Image_s hilbert = Render( CDV_HILBERT, cells );
    const Point_s first = Math_HilbertPosition( 0 );
    const Point_s missing = Math_HilbertPosition( 2 );
    assert( hilbert.SourceAt( first.x, first.y ) == 0 );
    assert( hilbert.SourceAt( missing.x, missing.y ) == CANTORDUST_NO_SOURCE );
}

static void Tests_BytePlotAnalysis()
{
    const std::vector< ByteCell_s > data = {
        { 0x41, true }, { 0x42, true }, { 0x41, true },
        { 0x43, true }, { 0xff, false }, { 0x41, true },
        { 0x42, true }, { 0x41, true } };
    BytePlotOptions_s options;
    const Image_s histogram = Render( CDV_HISTOGRAM, data, 256,
        CDP_REFERENCE, options );
    assert( histogram.bucketCounts.size() == 256 );
    assert( histogram.observationCount == 7 );
    assert( histogram.bucketCounts[ 0x41 ] == 4 );
    assert( histogram.bucketCounts[ 0x42 ] == 2 );
    assert( histogram.bucketCounts[ 0xff ] == 0 );
    assert( histogram.SourceAt( 0x41, 0 ) == 0 );
    assert( histogram.argb[ 255 * 256 + 0x41 ] ==
        Palette_ByteColor( 0x41, Palette_Get( CDP_REFERENCE ) ) );
    options.histogramScale = HistogramScale_e::Linear;
    const Image_s linear = Render( CDV_HISTOGRAM, data, 256,
        CDP_REFERENCE, options );
    options.histogramScale = HistogramScale_e::SquareRoot;
    const Image_s sqrtImage = Render( CDV_HISTOGRAM, data, 256,
        CDP_REFERENCE, options );
    options.histogramScale = HistogramScale_e::Logarithmic;
    const Image_s logImage = Render( CDV_HISTOGRAM, data, 256,
        CDP_REFERENCE, options );
    // Less common bytes remain visible under all three scaling rules.
    const auto countPixels = []( const Image_s& image, usize x ) {
        usize nonBackground = 0;
        const u32 background = Palette_Get( CDP_REFERENCE ).background;
        for ( usize y = 0; y < 256; ++y )
            nonBackground += image.argb[ y * 256 + x ] != background;
        return nonBackground;
    };
    assert( countPixels( linear, 0x42 ) <= countPixels( sqrtImage, 0x42 ) );
    assert( countPixels( logImage, 0x42 ) <= countPixels( sqrtImage, 0x42 ) );

    const Image_s freq = Render( CDV_DIGRAPH, data, 256,
        CDP_REFERENCE, options );
    const usize ab = ( 255 - 0x42 ) * 256 + 0x41;
    const usize ac = ( 255 - 0x43 ) * 256 + 0x41;
    assert( freq.observationCount == 5 );
    assert( freq.bucketCounts[ ab ] == 2 );
    assert( freq.bucketCounts[ ac ] == 1 );
    assert( freq.firstByteTotals[ 0x41 ] == 3 );
    assert( freq.SourceAt( 0x41, 255 - 0x42 ) == 0 );

    options.digraphIntensity = DigraphIntensity_e::ConditionalProbability;
    const Image_s conditional = Render( CDV_DIGRAPH, data, 256,
        CDP_REFERENCE, options );
    assert( conditional.conditionalProbability );
    assert( conditional.bucketCounts == freq.bucketCounts );
    assert( conditional.argb[ ab ] != freq.argb[ ab ] );
    options.digraphDistance = 2;
    const Image_s distant = Render( CDV_DIGRAPH, data, 512,
        CDP_REFERENCE, options );
    // 41 -> 41 twice, without counting pairs crossing the unloaded byte.
    assert( distant.bucketCounts[ ( 255 - 0x41 ) * 256 + 0x41 ] == 2 );
    assert( distant.observationCount == 3 );
    assert( distant.SourceAt( 0x41 * 2, ( 255 - 0x41 ) * 2 ) == 0 );
    assert( distant.pairDistance == 2 );
    options.digraphDistance = 0;
    bool rejected = false;
    try { (void)Render( CDV_DIGRAPH, data, 256, CDP_REFERENCE, options ); }
    catch ( const std::invalid_argument& ) { rejected = true; }
    assert( rejected );

    // Overview indices refer to logical offsets, not the local sample index.
    const std::vector< ByteClassSummary_s > classes( data.size() );
    std::vector< usize > offsets( data.size() );
    for ( usize i = 0; i < offsets.size(); ++i ) offsets[ i ] = i * 100;
    options.digraphDistance = 1;
    const Image_s overview = RenderOverview( CDV_DIGRAPH, classes,
        data, offsets, 256, CDP_REFERENCE, options );
    assert( overview.SourceAt( 0x41, 255 - 0x42 ) == 0 );
    assert( overview.SourceAt( 0x41, 255 - 0x43 ) == 200 );
}

static void Tests_ByteClassPalette()
{
    const std::vector< ByteCell_s > cells = {
        { 0x00, true }, { 0x01, true }, { 0x1F, true },
        { 0x20, true }, { 0x7F, true }, { 0x80, true },
        { 0xFE, true }, { 0xFF, true }, { 0x12, false }
    };

    const std::uint32_t expected[] = {
        0xFF000000u, 0xFF0000F0u, 0xFF0000F0u,
        0xFF00F000u, 0xFF00F000u, 0xFFF00000u,
        0xFFF00000u, 0xFFFFFFFFu, 0xFF303844u
    };

    const Image_s linear = Render( CDV_LINEAR, cells );
    const Image_s hilbert = Render( CDV_HILBERT, cells );

    for ( std::size_t i = 0; i < cells.size(); ++i )
    {
        assert( linear.argb[ i ] == expected[ i ] );

        const Point_s point = Math_HilbertPosition( i );
        assert( hilbert.argb[ point.y * CANTORDUST_SIDE + point.x ] ==
                expected[ i ] );
    }

    ByteClassSummary_s mixed;
    mixed.Add( 0x20 );
    mixed.Add( 0x80 );
    assert( mixed.SampleCount() == 2 );
    assert( mixed.printable == 1 && mixed.high == 1 );

    const Image_s overview = RenderHilbertOverview(
        { mixed, ByteClassSummary_s{} }, { 123u, CANTORDUST_NO_SOURCE } );

    const Point_s first = Math_HilbertPosition( 0 );
    const Point_s second = Math_HilbertPosition( 1 );
    assert( overview.argb[ first.y * CANTORDUST_SIDE + first.x ] ==
            0xFF787800u );

    assert( overview.SourceAt( first.x, first.y ) == 123u );
    assert( overview.SourceAt( second.x, second.y ) ==
            CANTORDUST_NO_SOURCE );
}

static void Tests_PalettePresets()
{
    assert( Palette_Count() == 16 );
    assert( Palette_Get( CDP_REFERENCE ).zero == 0xFF000000u );
    assert( Palette_Get( CDP_REFERENCE ).missing == 0xFF303844u );
    assert( Palette_Get( CDP_REFERENCE ).histogram == 0xFF67D9C3u );
    assert( Palette_Get( CDP_GHIDRA_VIOLET ).printable == 0xFF9935FFu );
    assert( Palette_Get( static_cast< Palette_e >( CDP_COUNT + 1 ) ).zero ==
            Palette_Get( CDP_REFERENCE ).zero );

    const std::vector< ByteCell_s > classes = {
        { 0, true }, { 1, true }, { 0x41, true },
        { 0x80, true }, { 0xFF, true }, { 0x41, false }
    };

    const std::vector< std::size_t > offsets = { 10, 20, 30, 40, 50, 60 };
    ByteClassSummary_s mixed;
    mixed.Add( 0x41 );
    mixed.Add( 0x80 );

    for ( std::size_t i = 0; i < Palette_Count(); ++i )
    {
        const Palette_e selected = static_cast< Palette_e >( i );
        const Palette_s& palette = Palette_Get( selected );
        assert( palette.name != nullptr && palette.name[ 0 ] != '\0' );

        const std::uint32_t expected[] = {
            palette.zero, palette.low, palette.printable,
            palette.high, palette.full, palette.missing
        };

        const Image_s linear = Render( CDV_LINEAR, classes, 256, selected );
        const Image_s hilbert = Render( CDV_HILBERT, classes, 256, selected );
        const Image_s detail = RenderDetail(
            CDV_HILBERT, classes, 256, selected );

        for ( std::size_t j = 0; j < classes.size(); ++j )
        {
            assert( linear.argb[ j ] == expected[ j ] );

            const Point_s p = Math_HilbertPosition( j );
            assert( hilbert.argb[ p.y * 256 + p.x ] == expected[ j ] );
        }

        assert( detail.SourceAt( Math_HilbertPosition( 0 ).x,
                                 Math_HilbertPosition( 0 ).y ) == 0 );

        assert( linear.SourceAt( 3, 0 ) == 3 );
        assert( hilbert.SourceAt( Math_HilbertPosition( 3 ).x,
                                  Math_HilbertPosition( 3 ).y ) == 3 );

        const Image_s mapped = RenderLinearMapped(
            classes, offsets, 256, selected );

        assert( mapped.argb[ 0 ] == palette.zero );
        assert( mapped.SourceAt( 4, 0 ) == 50 );
        assert( mapped.SourceAt( 5, 0 ) == CANTORDUST_NO_SOURCE );

        const Image_s overview = RenderHilbertOverview(
            { mixed }, { 123 }, 256, selected );

        const Point_s first = Math_HilbertPosition( 0 );
        const std::uint32_t colors[] = {
            palette.printable, palette.high
        };

        const std::uint32_t weights[] = { 1, 1 };
        assert( overview.argb[ first.y * 256 + first.x ] ==
                Math_WeightedColor( colors, weights, 2 ) );

        assert( overview.SourceAt( first.x, first.y ) == 123 );

        const Image_s graph = Render( CDV_DIGRAPH, classes, 256, selected );
        assert( graph.SourceAt( 0, 254 ) == 0 );
        assert( graph.argb[ 254 * 256 ] == palette.densityHigh );

        const Image_s histogram = Render(
            CDV_HISTOGRAM, classes, 256, selected );

        assert( histogram.argb[ 255 * 256 ] == palette.zero );
        assert( histogram.SourceAt( 0, 255 ) == 0 );
    }

    for ( std::size_t i = 0; i < Palette_Count(); ++i )
    {
        for ( std::size_t j = i + 1; j < Palette_Count(); ++j )
        {
            const Palette_s& a = Palette_Get( static_cast< Palette_e >( i ) );
            const Palette_s& b = Palette_Get( static_cast< Palette_e >( j ) );
            assert( a.printable != b.printable || a.high != b.high ||
                    a.low != b.low );
        }
    }
}

static void Tests_Entropy()
{
    std::vector< ByteCell_s > zeroes( 256, { 0, true } );
    assert( Math_ShannonEntropy( zeroes ) == 0.0 );

    std::vector< ByteCell_s > allBytes;

    for ( unsigned int i = 0; i < 256; ++i )
    {
        allBytes.push_back({ static_cast< std::uint8_t >( i ), true });
    }

    assert( std::abs( Math_ShannonEntropy( allBytes ) - 8.0 ) < 1e-9 );

    allBytes[ 0 ].valid = false;
    assert( Math_ShannonEntropy( allBytes ) < 8.0 );

    const std::vector< EntropySample_s > profile = Math_EntropyProfile(
        { { 0, true }, { 0, true }, { 0, false }, { 1, true } }, 2 );

    assert( profile.size() == 2 );
    assert( profile[ 0 ].valid && profile[ 0 ].bits == 0.0 );
    assert( profile[ 1 ].valid && profile[ 1 ].bits == 0.0 );

    const auto invalid = Math_EntropyProfile({ { 0x20, false } }, 1);
    assert( !invalid[ 0 ].valid );
}

static void Tests_WheelZoom()
{
    const ZoomRange_s initial{ 1000, 1000 };
    const ZoomRange_s zoomIn = Math_WheelZoom(
        10000, initial, 1250, 1, 1000, 1 );

    assert( zoomIn.start == 1050 );
    assert( zoomIn.length == 800 );

    const ZoomRange_s zoomOut = Math_WheelZoom(
        10000, zoomIn, 1250, 1, 1000, -1 );

    assert( zoomOut.start == initial.start );
    assert( zoomOut.length == initial.length );

    const ZoomRange_s leftEdge = Math_WheelZoom(
        10000, { 0, 1000 }, 0, 1, 1000, 1 );

    assert( leftEdge.start == 0 && leftEdge.length == 800 );

    const ZoomRange_s rightEdge = Math_WheelZoom(
        10000, { 9000, 1000 }, 9999, 1, 1000, 1 );

    assert( rightEdge.start == 9200 && rightEdge.length == 800 );

    const ZoomRange_s tiny = Math_WheelZoom(
        32, { 7, 2 }, 7, 1, 32, 50 );

    assert( tiny.start == 7 && tiny.length == 1 );

    const ZoomRange_s full = Math_WheelZoom(
        32, { 7, 2 }, 8, 1, 32, -50 );

    assert( full.start == 0 && full.length == 32 );

    const ZoomRange_s bounded = Math_WheelZoom(
        10000, { 4000, 800 }, 4100, 800, 10000, 1 );

    assert( bounded.start == 4000 && bounded.length == 800 );

    const ZoomRange_s huge = Math_WheelZoom(
        std::numeric_limits< std::uint64_t >::max(),
        { 3000000000000000000ull, 8000000000000000000ull },
        3400000000000000000ull, 1,
        std::numeric_limits< std::uint64_t >::max(), 1 );

    assert( huge.length == 6400000000000000000ull );
    assert( huge.start <= 3400000000000000000ull );
    assert( huge.start + huge.length <=
        std::numeric_limits< std::uint64_t >::max() );
}

static void Tests_OverviewDetailRangeSynchronization()
{
    constexpr u64 total = 0x100000000ull + 0x100000ull;

    // Reproduce the bug: Overview was independently zoomed down to a
    // small portion while Detail is expanded to cover the full executable.
    const ZoomRange_s full = Math_OverviewContainingDetail(
        total, { 0x80000000ull, 4096 }, { 0, total } );

    assert( full.start == 0 && full.length == total );

    // Recommitting a Detail range already contained by Overview keeps the
    // user's independent Overview zoom unchanged.
    const ZoomRange_s existing = Math_OverviewContainingDetail(
        total, { 5000, 50000 }, { 12000, 4000 } );

    assert( existing.start == 5000 && existing.length == 50000 );

    // Navigating outside a narrow Overview moves the viewport without
    // unnecessarily zooming out.
    const ZoomRange_s panRight = Math_OverviewContainingDetail(
        100000, { 5000, 5000 }, { 60000, 1000 } );

    assert( panRight.start == 56000 && panRight.length == 5000 );

    const ZoomRange_s panLeft = Math_OverviewContainingDetail(
        100000, { 50000, 5000 }, { 1000, 1000 } );

    assert( panLeft.start == 1000 && panLeft.length == 5000 );

    // Expanding Detail to a larger interval grows Overview just enough.
    const ZoomRange_s grow = Math_OverviewContainingDetail(
        100000, { 32000, 1000 }, { 20000, 45000 } );

    assert( grow.start == 20000 && grow.length == 45000 );

    // Edge ranges and very large logical offsets must never wrap.
    constexpr u64 maximum = std::numeric_limits< u64 >::max();
    const ZoomRange_s high = Math_OverviewContainingDetail(
        maximum, { maximum - 500, 200 }, { maximum - 1000, 1000 } );

    assert( high.start == maximum - 1000 && high.length == 1000 );

    const ZoomRange_s low = Math_OverviewContainingDetail(
        10000, { 50, 9000 }, { 0, 100 } );

    assert( low.start == 0 && low.length == 9000 );

    const ZoomRange_s empty = Math_OverviewContainingDetail(
        0, { 100, 200 }, { 0, 100 } );

    assert( empty.start == 0 && empty.length == 0 );

    // Exhaustively check interval containment and minimum viewport growth
    // for all valid small selections, including offscreen Detail intervals.
    for ( u64 totalLength = 1; totalLength <= 20; ++totalLength )
    {
        for ( u64 viewStart = 0; viewStart < totalLength; ++viewStart )
        {
            for ( u64 viewLength = 1;
                  viewLength <= totalLength - viewStart; ++viewLength )
            {
                for ( u64 detailStart = 0;
                      detailStart < totalLength; ++detailStart )
                {
                    for ( u64 detailLength = 1;
                          detailLength <= totalLength - detailStart;
                          ++detailLength )
                    {
                        const ZoomRange_s view{ viewStart, viewLength };
                        const ZoomRange_s detail{ detailStart, detailLength };
                        const ZoomRange_s fitted = Math_OverviewContainingDetail(
                            totalLength, view, detail );

                        assert( fitted.length ==
                            std::max( viewLength, detailLength ) );
                        assert( fitted.start + fitted.length <= totalLength );
                        assert( fitted.start <= detailStart );
                        assert( fitted.start + fitted.length >=
                            detailStart + detailLength );

                        if ( detailStart >= viewStart &&
                             detailStart + detailLength <=
                                 viewStart + viewLength )
                        {
                            assert( fitted.start == viewStart );
                        }
                    }
                }
            }
        }
    }
}

static void Tests_DetailZoomRaster()
{
    const std::vector< ByteCell_s > bytes = {
        { 0x20, true }, { 0x80, true }, { 0x00, false }, { 0xFF, true }
    };

    for ( const View_e mode : { CDV_LINEAR, CDV_HILBERT } )
    {
        const Image_s image = RenderDetail( mode, bytes, 256 );
        const std::size_t pixels = image.width * image.height;

        for ( std::size_t i : { 0u, 1u, 1000u, 32768u, 65535u } )
        {
            const std::size_t expected = i * bytes.size() / pixels;
            const Point_s point = mode == CDV_HILBERT
                ? Math_HilbertPosition( i )
                : Point_s{ i % 256, i / 256 };

            assert( image.SourceAt( point.x, point.y ) ==
                ( bytes[ expected ].valid ? expected : CANTORDUST_NO_SOURCE ) );
        }
    }

    for ( const std::size_t side : { 512u, 1024u } )
    {
        for ( const std::size_t index : std::initializer_list< std::size_t >{
                 0, 1, 1023, 8192, 65535, 262143,
                 side * side - 1 } )
        {
            if ( index < side * side )
            {
                assert( Math_HilbertIndex(
                    Math_HilbertPosition( index, side ), side ) == index );
            }
        }
    }

    const Image_s one = RenderDetail( CDV_LINEAR,
        { ByteCell_s{ 0x20, true } }, 512 );

    assert( one.SourceAt( 0, 0 ) == 0 );
    assert( one.SourceAt( 511, 511 ) == 0 );
}

static void Tests_HilbertCurve()
{
    std::vector< bool > visited( CANTORDUST_WINDOW_SIZE, false );

    for ( std::size_t i = 0; i < CANTORDUST_WINDOW_SIZE; ++i )
    {
        const Point_s point = Math_HilbertPosition( i );
        assert( point.x < CANTORDUST_SIDE && point.y < CANTORDUST_SIDE );
        assert( Math_HilbertIndex( point, CANTORDUST_SIDE ) == i );
        assert( !visited[ point.y * CANTORDUST_SIDE + point.x ] );
        visited[ point.y * CANTORDUST_SIDE + point.x ] = true;
    }

    for ( std::size_t i = 1; i < CANTORDUST_WINDOW_SIZE; ++i )
    {
        const Point_s a = Math_HilbertPosition( i - 1 );
        const Point_s b = Math_HilbertPosition( i );
        const std::size_t distance =
            ( a.x > b.x ? a.x - b.x : b.x - a.x ) +
            ( a.y > b.y ? a.y - b.y : b.y - a.y );

        assert( distance == 1 );
    }

    bool threw = false;
    try
    {
        (void)Math_HilbertPosition( CANTORDUST_WINDOW_SIZE );
    }
    catch ( const std::out_of_range& )
    {
        threw = true;
    }

    assert( threw );
}

static void Tests_MaximumWindow()
{
    std::vector< ByteCell_s > cells(
        CANTORDUST_WINDOW_SIZE, ByteCell_s{ 0, true } );

    assert( Render( CDV_LINEAR, cells ).SourceAt( 255, 255 ) ==
            CANTORDUST_WINDOW_SIZE - 1 );

    const Point_s last = Math_HilbertPosition( CANTORDUST_WINDOW_SIZE - 1 );
    assert( Render( CDV_HILBERT, cells ).SourceAt( last.x, last.y ) ==
            CANTORDUST_WINDOW_SIZE - 1 );

    assert( Render( CDV_DIGRAPH, cells ).SourceAt( 0, 255 ) == 0 );
    cells[ 0 ].valid = false;
    assert( Render( CDV_DIGRAPH, cells ).SourceAt( 0, 255 ) == 1 );
    assert( Render( CDV_HISTOGRAM, cells ).SourceAt( 0, 255 ) == 1 );

    cells.push_back( ByteCell_s{ 1, true } );
    bool threw = false;
    try
    {
        (void)Render( CDV_LINEAR, cells );
    }
    catch ( const std::invalid_argument& )
    {
        threw = true;
    }

    assert( threw );
}

static void Tests_DensityMath()
{
    assert( Math_LogDensity( 0, 100 ) == 0.0 );
    assert( Math_LogDensity( 100, 100 ) == 1.0 );
    assert( Math_HistogramHeight( 0, 100, 255 ) == 0 );
    assert( Math_HistogramHeight( 100, 100, 255 ) == 255 );
}

static void Tests_WindowPositionMath()
{
    assert( Math_ScaledPosition( 100, 0, 10000 ) == 0 );
    assert( Math_ScaledPosition( 100, 5000, 10000 ) == 50 );
    assert( Math_ScaledPosition( 100, 10000, 10000 ) == 100 );
    assert( Math_ScaledPosition( 100, 20000, 10000 ) == 100 );
    assert( Math_ScaledPosition( 100, 1, 0 ) == 0 );

    assert( Math_PositionForOffset( 0, 99, 10000 ) == 0 );
    assert( Math_PositionForOffset( 100, 0, 10000 ) == 99 );
    assert( Math_PositionForOffset( 100, 50, 10000 ) == 5099 );
    assert( Math_PositionForOffset( 100, 100, 10000 ) == 10000 );

    const std::uint64_t maximum = std::numeric_limits< std::uint64_t >::max();
    std::uint64_t previous = 0;

    for ( std::uint32_t position = 0; position <= 10000; ++position )
    {
        const std::uint64_t value = Math_ScaledPosition(
            maximum, position, 10000 );

        assert( value >= previous );
        assert( Math_PositionForOffset( maximum, value, 10000 ) == position );
        previous = value;
    }

    assert( previous == maximum );
}

static void Tests_PixelMapping()
{
    Point_s point{ 99, 99 };
    assert( Math_MapPointToImage( 12, 18, 12, 18, 512, 512,
                                  256, 256, point ) );

    assert( point.x == 0 && point.y == 0 );
    assert( Math_MapPointToImage( 523, 529, 12, 18, 512, 512,
                                  256, 256, point ) );

    assert( point.x == 255 && point.y == 255 );
    assert( Math_MapPointToImage( 13, 20, 12, 18, 512, 512,
                                  256, 256, point ) );

    assert( point.x == 0 && point.y == 1 );
    assert( !Math_MapPointToImage( 524, 529, 12, 18, 512, 512,
                                   256, 256, point ) );

    assert( !Math_MapPointToImage( 12, 17, 12, 18, 512, 512,
                                   256, 256, point ) );

    assert( !Math_MapPointToImage( 0, 0, 0, 0, 0, 256,
                                   256, 256, point ) );

    assert( !Math_MapPointToImage( 0, 0, 0, 0, 256, 256,
                                   0, 256, point ) );

    assert( Math_MapPointToImage( 0, 0, 0, 0, 1, 1,
                                  256, 256, point ) );

    assert( point.x == 0 && point.y == 0 );

    // Non-square views must retain exact source mapping after stretching.
    assert( Math_MapPointToImage( 10, 20, 10, 20, 900, 360,
                                  256, 256, point ) );

    assert( point.x == 0 && point.y == 0 );
    assert( Math_MapPointToImage( 460, 200, 10, 20, 900, 360,
                                  256, 256, point ) );

    assert( point.x == 128 && point.y == 128 );
    assert( Math_MapPointToImage( 909, 379, 10, 20, 900, 360,
                                  256, 256, point ) );

    assert( point.x == 255 && point.y == 255 );
    assert( !Math_MapPointToImage( 910, 379, 10, 20, 900, 360,
                                   256, 256, point ) );

    assert( !Math_MapPointToImage( 909, 380, 10, 20, 900, 360,
                                   256, 256, point ) );
}

static void Tests_OverviewHelpers()
{
    std::vector< ByteCell_s > cells = {
        { 0x10, true }, { 0x20, true }, { 0x30, false }, { 0x40, true }
    };

    const std::vector< std::size_t > offsets = { 100, 200, 300, 400 };
    const Image_s mapped = RenderLinearMapped( cells, offsets );
    assert( mapped.SourceAt( 0, 0 ) == 100 );
    assert( mapped.SourceAt( 1, 0 ) == 200 );
    assert( mapped.SourceAt( 2, 0 ) == CANTORDUST_NO_SOURCE );
    assert( mapped.SourceAt( 3, 0 ) == 400 );

    assert( Math_OverviewPixelForOffset( 1000, 0, 100 ) == 0 );
    assert( Math_OverviewPixelForOffset( 1000, 999, 100 ) == 99 );

    std::size_t startPixel = 0;
    std::size_t endPixel = 0;
    Math_OverviewPixelSpan( 1000, 100, 200, 100, startPixel, endPixel );
    assert( startPixel == 10 );
    assert( endPixel == 30 );

    Math_OverviewPixelSpan( 1000, 999, 1, 100, startPixel, endPixel );
    assert( startPixel == 99 );
    assert( endPixel == 100 );
    Math_OverviewPixelSpan( 4, 0, 4, 65536, startPixel, endPixel );
    assert( startPixel == 0 );
    assert( endPixel == 65536 );
}

static void Tests_DragNavigationGate()
{
    NavigationGate gate;
    assert( gate.ShouldNavigate( 1, 0, true ) );
    assert( !gate.ShouldNavigate( 1, 100, true ) );
    assert( !gate.ShouldNavigate( 2, 12, false ) );
    assert( gate.ShouldNavigate( 3, 24, false ) );
    assert( !gate.ShouldNavigate( 4, 30, false ) );
    assert( gate.ShouldNavigate( 4, 30, true ) );
    assert( !gate.ShouldNavigate( 4, 31, false ) );
    assert( gate.ShouldNavigate( 5, 55, false ) );
    gate.Reset();
    assert( gate.ShouldNavigate( 5, 0, false ) );
    assert( gate.ShouldNavigate( 6, 0, true ) );
}

static void Tests_RangeMapping()
{
    std::vector< ByteRange_s > ranges;
    assert( Math_AddRange( ranges, 0x1000, 5 ) );
    assert( Math_AddRange( ranges, 0x9000, 3 ) );
    assert( Math_RangeLength( ranges ) == 8 );

    std::uint64_t address = 0;
    std::uint64_t offset = 0;
    assert( Math_OffsetToAddress( ranges, 4, address ) && address == 0x1004 );
    assert( Math_OffsetToAddress( ranges, 5, address ) && address == 0x9000 );
    assert( Math_OffsetToAddress( ranges, 7, address ) && address == 0x9002 );
    assert( !Math_OffsetToAddress( ranges, 8, address ) );
    assert( Math_AddressToOffset( ranges, 0x9001, offset ) && offset == 6 );
    assert( Math_AddressToOffset( ranges, 0x1000, offset ) && offset == 0 );
    assert( !Math_AddressToOffset( ranges, 0x2000, offset ) );
    assert( !Math_AddRange( ranges,
        std::numeric_limits< std::uint64_t >::max() - 1, 4 ) );
}

static void Tests_ResolutionAndOverviewModes()
{
    const std::vector< ByteCell_s > cells = {
        { 0x20, true }, { 0x80, true }, { 0x00, false }, { 0xff, true }
    };

    const std::vector< std::size_t > offsets = { 100, 200, 300, 400 };
    std::vector< ByteClassSummary_s > classes( cells.size() );

    for ( std::size_t i = 0; i < cells.size(); ++i )
    {
        if ( cells[ i ].valid )
        {
            classes[ i ].Add( cells[ i ].value );
        }
    }

    for ( const std::size_t side : { 256u, 512u, 1024u } )
    {
        const Image_s linear = RenderOverview(
            CDV_LINEAR, classes, cells, offsets, side );

        assert( linear.width == side && linear.height == side );
        assert( linear.SourceAt( 0, 0 ) == 100 );
        assert( linear.SourceAt( 1, 0 ) == 200 );
        assert( linear.SourceAt( 2, 0 ) == CANTORDUST_NO_SOURCE );

        const Image_s hilbert = RenderOverview(
            CDV_HILBERT, classes, cells, offsets, side );

        const Point_s h1 = Math_HilbertPosition( 1, side );
        assert( hilbert.SourceAt( h1.x, h1.y ) == 200 );

        const Image_s histogram = RenderOverview(
            CDV_HISTOGRAM, classes, cells, offsets, side );

        assert( histogram.SourceAt( side - 1, side - 1 ) == 400 );

        const Image_s digraph = RenderOverview(
            CDV_DIGRAPH, classes, cells, offsets, side );

        assert( digraph.width == side );
    }

    assert( Math_HilbertPosition( 1024u * 1024u - 1, 1024 ).x < 1024 );
}

static void Tests_CubeTriplets()
{
    const std::vector< ByteCell_s > cells = {
        { 0x10, true }, { 0x20, true }, { 0x30, true },
        { 0x40, false }, { 0x50, true }, { 0x60, true },
        { 0x70, true }, { 0x80, true }, { 0x90, true }
    };

    const CubeCloud_s cloud = Cube_BuildCloud( cells, CDP_REFERENCE );
    assert( cloud.acceptedTriplets == 2 );
    assert( cloud.points.size() == 2 );
    assert( cloud.occupiedVoxels == 2 );

    for ( const CubePoint_s& point : cloud.points )
    {
        assert( point.sourceIndex == 4 || point.sourceIndex == 5 );
        assert( point.count == 1 );
        assert( point.x >= -1.0f && point.x <= 1.0f );
        assert( point.sourceByte == cells[ point.sourceIndex ].value );
    }

    const CubeCloud_s split = Cube_BuildCloud(
        { { 0x10, true }, { 0x20, true }, { 0x30, true },
          { 0x40, true }, { 0x50, true }, { 0x60, true },
          { 0x70, true }, { 0x80, true } },
        CDP_REFERENCE, CANTORDUST_CUBE_MAX_POINTS, { 4 } );

    assert( split.acceptedTriplets == 2 );
    assert( split.points.size() == 2 );

    for ( const CubePoint_s& point : split.points )
    {
        assert( point.sourceIndex == 0 || point.sourceIndex == 4 );
    }

    const CubeCloud_s repeated = Cube_BuildCloud(
        { { 0x41, true }, { 0x42, true }, { 0x43, true },
          { 0x41, true }, { 0x42, true }, { 0x43, true } },
        CDP_REFERENCE );

    assert( repeated.acceptedTriplets == 3 );
    assert( repeated.occupiedVoxels <= 3 );

    const CubeCloud_s small = Cube_BuildCloud(
        { { 0x21, true }, { 0x22, true }, { 0x23, true } },
        CDP_REFERENCE );

    assert( small.points.empty() );
    assert( Cube_BuildCloud( cells, CDP_REFERENCE, 0 ).points.empty() );
}

static void Tests_CubeProjectionAndPicking()
{
    const std::vector< ByteCell_s > cells = {
        { 0xFF, true }, { 0xFF, true },
        { 0xFF, true }, { 0xFF, true }
    };

    const CubeCloud_s cloud = Cube_BuildCloud( cells, CDP_GHIDRA_VIOLET );
    CubeCamera_s camera;

    const Image_s image = Cube_Render( cloud, camera, 320, 240,
        CDP_GHIDRA_VIOLET );

    assert( image.width == 320 && image.height == 240 );
    assert( image.argb.size() == 320u * 240u );
    assert( image.sourceIndex.size() == image.argb.size() );
    assert( std::find( image.sourceIndex.begin(), image.sourceIndex.end(),
            std::size_t{ 0 } ) != image.sourceIndex.end() );

    assert( Cube_Render( cloud, camera, 0, 240,
        CDP_REFERENCE ).argb.empty() );

    Image_s reusable;
    std::vector< float > depthBuffer;
    Cube_RenderInto( reusable, depthBuffer, cloud, camera,
        320, 240, CDP_GHIDRA_VIOLET );

    assert( reusable.argb == image.argb );
    assert( reusable.sourceIndex == image.sourceIndex );

    const std::uint32_t* const reusedPixels = reusable.argb.data();
    Cube_Rotate( camera, 8.0f, 0.0f );
    Cube_RenderInto( reusable, depthBuffer, cloud, camera,
        320, 240, CDP_GHIDRA_VIOLET );

    assert( reusable.argb.data() == reusedPixels );

    const CubeTransform_s transform = Math_CubeTransform( camera );
    const CubeProjection_s projection = Math_CubeProject( 0, 0, 0, transform );
    assert( std::abs( projection.x ) < 0.0001f );
    assert( std::abs( projection.y ) < 0.0001f );
    Cube_Rotate( camera, 90.0f, 300.0f );
    assert( camera.pitch > 1.48f );
    Cube_Rotate( camera, 0.0f, 900.0f );
    assert( std::isfinite( camera.pitch ) );
    Cube_Rotate( camera, 2000.0f, 0.0f );
    assert( std::isfinite( camera.yaw ) );
    Cube_Zoom( camera, 120 );
    assert( camera.zoom <= 4.0f );
    Cube_Zoom( camera, -120 );
    assert( camera.zoom >= 0.35f );
}

static void Tests_CubeOverviewTripletsAndPalettes()
{
    const std::vector< CubeTriplet_s > triples = {
        { 0x20u, 0x30u, 0xE0u, 0x41u, 450u },
        { 0x20u, 0x30u, 0xE0u, 0xFFu, 451u },
        { 0x80u, 0x80u, 0x10u, 0x00u, 900u },
        { 0x01u, 0x02u, 0x03u, 0xFFu, CANTORDUST_NO_SOURCE }
    };

    const CubeCloud_s first = Cube_BuildTriplets(
        triples, CDP_REFERENCE );

    assert( first.acceptedTriplets == 3 );
    assert( first.points.size() == 2 );
    assert( first.occupiedVoxels == 2 );
    bool foundFirst = false;

    for ( const CubePoint_s& point : first.points )
    {
        if ( point.sourceIndex == 450u )
        {
            assert( point.count == 2u );
            assert( point.sourceByte == 0x41u );
            foundFirst = true;
        }
    }

    assert( foundFirst );

    const CubeCloud_s violet = Cube_BuildTriplets(
        triples, CDP_GHIDRA_VIOLET );

    assert( violet.points.size() == first.points.size() );
    bool changed = false;

    for ( std::size_t i = 0; i < first.points.size(); ++i )
    {
        changed |= first.points[ i ].argb != violet.points[ i ].argb;
        assert( first.points[ i ].sourceIndex ==
            violet.points[ i ].sourceIndex );
    }

    assert( changed );
    assert( Cube_BuildTriplets( triples, CDP_REFERENCE, 1 ).points.size() == 1 );
}

static void Tests_CubeFinePixelsAndGhidraPalette()
{
    const std::vector< CubeTriplet_s > triplets = {
        { 10u, 10u, 20u, 0x00u, 10u },
        { 10u, 60u, 70u, 0x01u, 20u },
        { 10u, 100u, 120u, 0x41u, 30u },
        { 10u, 140u, 180u, 0x80u, 40u },
        { 10u, 220u, 250u, 0xFFu, 50u }
    };

    for ( const Palette_e palette : { CDP_REFERENCE, CDP_GHIDRA_VIOLET } )
    {
        const CubeCloud_s cloud = Cube_BuildTriplets( triplets, palette );
        assert( cloud.points.size() == triplets.size() );

        const Image_s frame = Cube_Render( cloud, {}, 1024, 768, palette );

        std::size_t mappedPixels = 0;

        for ( const CubeTriplet_s& triplet : triplets )
        {
            const auto point = std::find_if(
                cloud.points.begin(), cloud.points.end(),
                [&triplet]( const CubePoint_s& value ) {
                    return value.sourceIndex == triplet.sourceIndex;
                } );

            assert( point != cloud.points.end() );
            assert( point->sourceByte == triplet.colorByte );

            std::size_t drawn = 0;

            for ( std::size_t pixel = 0; pixel < frame.argb.size(); ++pixel )
            {
                if ( frame.sourceIndex[ pixel ] == triplet.sourceIndex )
                {
                    ++drawn;
                }
            }

            assert( drawn >= 1 );
            mappedPixels += drawn;
        }

        assert( mappedPixels >= triplets.size() );
    }

    // Color is now driven by XYZ, not by the navigation byte.
    const CubeCloud_s first = Cube_BuildTriplets(
        { { 10u, 40u, 80u, 0x41u, 100u } }, CDP_REFERENCE );

    const CubeCloud_s second = Cube_BuildTriplets(
        { { 10u, 40u, 80u, 0x80u, 100u } }, CDP_REFERENCE );

    assert( first.points.size() == 1 && second.points.size() == 1 );
    assert( first.points[ 0 ].x == second.points[ 0 ].x );
    assert( first.points[ 0 ].y == second.points[ 0 ].y );
    assert( first.points[ 0 ].z == second.points[ 0 ].z );
    assert( first.points[ 0 ].argb == second.points[ 0 ].argb );
    assert( first.points[ 0 ].sourceByte == 0x41u );
    assert( second.points[ 0 ].sourceByte == 0x80u );

    // Geometry changes must change the cube color.
    const CubeCloud_s greenish = Cube_BuildTriplets(
        { { 5u, 220u, 5u, 0x41u, 100u } }, CDP_REFERENCE );

    const CubeCloud_s reddish = Cube_BuildTriplets(
        { { 220u, 5u, 5u, 0x41u, 100u } }, CDP_REFERENCE );

    assert( greenish.points.size() == 1 && reddish.points.size() == 1 );
    assert( greenish.points[ 0 ].argb != reddish.points[ 0 ].argb );

    const std::vector< ByteCell_s > bytes = {
        { 0x41u, true }, { 0x90u, true },
        { 0x10u, true }, { 0xE0u, true },
        { 0xFFu, true }
    };

    const CubeCloud_s cloud = Cube_BuildCloud( bytes, CDP_REFERENCE );
    assert( cloud.acceptedTriplets == 2 );
    bool firstFound = false;

    for ( const CubePoint_s& point : cloud.points )
    {
        if ( point.sourceIndex == 0 )
        {
            assert( point.sourceByte == 0x41u );
            firstFound = true;
        }
    }

    assert( firstFound );
    assert( CANTORDUST_CUBE_MAX_POINTS == 131072u );
}

static void Tests_CubeBoundedCloud()
{
    std::vector< ByteCell_s > cells( 1024 * 1024 );
    std::uint32_t seed = 0x12345678u;

    for ( ByteCell_s& cell : cells )
    {
        seed = seed * 1664525u + 1013904223u;
        cell = { static_cast< std::uint8_t >( seed >> 24 ), true };
    }

    const CubeCloud_s cloud = Cube_BuildCloud(
        cells, CDP_AURORA, 8192 );

    assert( cloud.acceptedTriplets == cells.size() - 3 );
    assert( cloud.occupiedVoxels <= CANTORDUST_CUBE_GRID *
        CANTORDUST_CUBE_GRID * CANTORDUST_CUBE_GRID );

    assert( cloud.points.size() == 8192 );

    for ( const CubePoint_s& point : cloud.points )
    {
        assert( point.count > 0 );
        assert( point.sourceIndex < cells.size() - 2 );
    }
}

static void Tests_DetailZoomCanExceedPixelBudget()
{
    // The detail window's logical length is not capped by its raster size.
    // A wheel operation on a large selection must not snap it down to
    // 256*256 bytes or discard the selection's far end.
    constexpr std::uint64_t total = 1ull << 34;
    const ZoomRange_s start = { total - ( 1ull << 30 ), 1ull << 30 };
    const std::uint64_t anchor = start.start + start.length / 2;
    const ZoomRange_s zoomIn = Math_WheelZoom(
        total, start, anchor, 1, total, 1 );

    assert( zoomIn.length > CANTORDUST_WINDOW_SIZE );
    assert( zoomIn.start + zoomIn.length <= total );

    const ZoomRange_s zoomOut = Math_WheelZoom(
        total, zoomIn, anchor, 1, total, -1 );

    assert( zoomOut.length > zoomIn.length );
    assert( zoomOut.start + zoomOut.length <= total );

    const ZoomRange_s full = Math_WheelZoom(
        total, { 0, total }, total - 1, 1, total, 1 );

    assert( full.length > CANTORDUST_WINDOW_SIZE );
}

int main()
{
    Tests_CubeTriplets();
    Tests_CubeProjectionAndPicking();
    Tests_CubeOverviewTripletsAndPalettes();
    Tests_CubeBoundedCloud();
    Tests_CubeFinePixelsAndGhidraPalette();
    Tests_RenderModes();
    Tests_BytePlotAnalysis();
    Tests_ByteClassPalette();
    Tests_PalettePresets();
    Tests_Entropy();
    Tests_WheelZoom();
    Tests_OverviewDetailRangeSynchronization();
    Tests_DetailZoomCanExceedPixelBudget();
    Tests_DetailZoomRaster();
    Tests_HilbertCurve();
    Tests_MaximumWindow();
    Tests_DensityMath();
    Tests_WindowPositionMath();
    Tests_PixelMapping();
    Tests_OverviewHelpers();
    Tests_DragNavigationGate();
    Tests_RangeMapping();
    Tests_ResolutionAndOverviewModes();

    std::cout << "All CantorDust C++ core tests passed.\n";
    return 0;
}
