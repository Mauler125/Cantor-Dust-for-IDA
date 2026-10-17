#include "pch/core_pch.h"
#include "cantordust/renderer.h"
#include "cantordust/scalar_types.h"

#include "math/color.h"
#include "math/density.h"
#include "math/hilbert.h"
#include "tier0/compiler.h"

#include <stdexcept>

namespace cantordust
{

static Image_s Render_EmptyImage(
    const usize side, const Palette_s& palette )
{
    Image_s image;
    image.width = side;
    image.height = side;
    image.argb.assign( side * side, palette.background );
    image.sourceIndex.assign( side * side, CANTORDUST_NO_SOURCE );
    return image;
}

static u32 Render_ClassColor(
    const ByteClassSummary_s& sample, const Palette_s& palette )
{
    if ( sample.SampleCount() == 0 )
    {
        return palette.missing;
    }

    const u32 colors[] = {
        palette.zero, palette.low, palette.printable,
        palette.high, palette.full
    };

    const u32 weights[] = {
        sample.zero, sample.low, sample.printable,
        sample.high, sample.full
    };
    return Math_WeightedColor( colors, weights, 5 );
}

static u32 Render_DensityColor(
    const u32 count,
    const u32 highest,
    const Palette_s& palette )
{
    if ( count == 0 || highest == 0 )
    {
        return palette.background;
    }

    const f64 normalized = Math_LogDensity( count, highest );
    return Math_BlendColor(
        palette.densityLow, palette.densityHigh, normalized );
}

static void Render_ByteRaster(
    Image_s& image,
    const std::vector< ByteCell_s >& data,
    const bool hilbert,
    const Palette_s& palette,
    const u32* const byteColors )
{
    const ByteCell_s* const cells = data.data();
    const u32* const hilbertPixels = hilbert
        ? Math_HilbertPixelOffsets( image.width ).data() : nullptr;

    for ( usize i = 0; i < data.size(); ++i )
    {
        const usize pixel = hilbert ? hilbertPixels[ i ] : i;
        const ByteCell_s& cell = cells[ i ];

        image.argb[ pixel ] = cell.valid
            ? byteColors[ cell.value ]
            : palette.missing;

        if ( cell.valid )
        {
            image.sourceIndex[ pixel ] = i;
        }
    }
}

static void Render_ByteRasterMapped(
    Image_s& image,
    const std::vector< ByteCell_s >& data,
    const std::vector< usize >& sourceOffsets,
    const Palette_s& palette,
    const u32* const byteColors )
{
    const usize count = std::min( data.size(), sourceOffsets.size() );

    for ( usize i = 0; i < count; ++i )
    {
        const Point_s point{ i % image.width, i / image.width };
        const usize pixel = point.y * image.width + point.x;
        const ByteCell_s& cell = data[ i ];

        image.argb[ pixel ] = cell.valid
            ? byteColors[ cell.value ]
            : palette.missing;

        image.sourceIndex[ pixel ] = cell.valid
            ? sourceOffsets[ i ]
            : CANTORDUST_NO_SOURCE;
    }
}

static void Render_Digraph(
    Image_s& image,
    const std::vector< ByteCell_s >& data,
    const Palette_s& palette,
    const BytePlotOptions_s& options )
{
    // The 65,536 counters and first-occurrence indices live on the heap,
    // avoiding the default 1 MiB Windows stack limit.
    std::vector< u32 > counts( CANTORDUST_WINDOW_SIZE, 0u );
    std::vector< usize > first(
        CANTORDUST_WINDOW_SIZE, CANTORDUST_NO_SOURCE );
    std::array< u32, 256 > firstByteTotals{};
    usize total = 0;
    const usize distance = options.digraphDistance;
    if ( distance == 0 || distance > 256 )
        throw std::invalid_argument( "Digraph distance must be 1..256" );

    // Check every intermediate sample too: pairs must not jump across
    // unloaded IDA memory. Sliding invalid-count keeps this O(n), not O(n*d).
    if ( data.size() > distance )
    {
        usize invalid = 0;
        for ( usize k = 0; k <= distance; ++k )
            invalid += !data[ k ].valid;

        for ( usize i = 0; i + distance < data.size(); ++i )
        {
            if ( invalid == 0 )
            {
                const u8 firstByte = data[ i ].value;
                const u8 secondByte = data[ i + distance ].value;
                const usize bucket = ( 255u - secondByte ) *
                    CANTORDUST_SIDE + firstByte;
                ++counts[ bucket ];
                ++firstByteTotals[ firstByte ];
                ++total;
                if ( first[ bucket ] == CANTORDUST_NO_SOURCE )
                    first[ bucket ] = i;
            }
            if ( i + distance + 1 < data.size() )
            {
                invalid -= !data[ i ].valid;
                invalid += !data[ i + distance + 1 ].valid;
            }
        }
    }

    const u32 highest = *std::max_element( counts.begin(), counts.end() );
    std::vector< u32 > bucketColors( CANTORDUST_WINDOW_SIZE );
    const bool conditional = options.digraphIntensity ==
        DigraphIntensity_e::ConditionalProbability;
    for ( usize bucket = 0; bucket < counts.size(); ++bucket )
    {
        if ( !conditional )
        {
            bucketColors[ bucket ] = Render_DensityColor(
                counts[ bucket ], highest, palette );
        }
        else
        {
            const u32 rowTotal = firstByteTotals[ bucket % CANTORDUST_SIDE ];
            const f64 probability = rowTotal ?
                static_cast< f64 >( counts[ bucket ] ) / rowTotal : 0.0;
            bucketColors[ bucket ] = counts[ bucket ]
                ? Math_BlendColor( palette.densityLow, palette.densityHigh,
                    probability )
                : palette.background;
        }
    }

    image.bucketCounts = std::move( counts );
    image.firstByteTotals = firstByteTotals;
    image.observationCount = total;
    image.pairDistance = distance;
    image.conditionalProbability = conditional;

    for ( usize y = 0; y < image.height; ++y )
    {
        const usize bucketRow = y * CANTORDUST_SIDE / image.height;
        for ( usize x = 0; x < image.width; ++x )
        {
            const usize bucket = bucketRow * CANTORDUST_SIDE +
                x * CANTORDUST_SIDE / image.width;
            const usize pixel = y * image.width + x;
            image.argb[ pixel ] = bucketColors[ bucket ];
            image.sourceIndex[ pixel ] = first[ bucket ];
        }
    }
}

static void Render_Histogram(
    Image_s& image,
    const std::vector< ByteCell_s >& data,
    const Palette_e paletteIndex,
    const BytePlotOptions_s& options )
{
    std::array< u32, CANTORDUST_SIDE > counts{};
    std::array< usize, CANTORDUST_SIDE > first{};
    first.fill( CANTORDUST_NO_SOURCE );

    usize total = 0;
    for ( usize i = 0; i < data.size(); ++i )
    {
        const ByteCell_s& cell = data[ i ];
        if ( V_UNLIKELY( !cell.valid ) )
            continue;
        ++counts[ cell.value ];
        ++total;
        if ( first[ cell.value ] == CANTORDUST_NO_SOURCE )
            first[ cell.value ] = i;
    }

    image.bucketCounts.assign( counts.begin(), counts.end() );
    image.observationCount = total;
    const u32 highest = *std::max_element( counts.begin(), counts.end() );
    if ( highest == 0 )
        return;

    const auto& colors = Palette_ByteColorTable( paletteIndex );
    for ( usize x = 0; x < image.width; ++x )
    {
        const usize bucket = x * CANTORDUST_SIDE / image.width;
        const u32 count = counts[ bucket ];
        if ( count == 0 )
            continue;

        f64 fraction = 0.0;
        switch ( options.histogramScale )
        {
            case HistogramScale_e::Linear:
                fraction = static_cast< f64 >( count ) / highest;
                break;
            case HistogramScale_e::SquareRoot:
                fraction = std::sqrt( static_cast< f64 >( count ) / highest );
                break;
            case HistogramScale_e::Logarithmic:
                fraction = Math_LogDensity( count, highest );
                break;
        }
        const usize height = std::max< usize >( 1, static_cast< usize >(
            std::ceil( fraction * ( image.height - 1 ) ) ) );
        for ( usize y = image.height - height; y < image.height; ++y )
        {
            const usize pixel = y * image.width + x;
            image.argb[ pixel ] = colors[ bucket ];
        }
        // The full column identifies the byte value, even above its bar;
        // one click can always navigate to the first matching byte.
        for ( usize y = 0; y < image.height; ++y )
            image.sourceIndex[ y * image.width + x ] = first[ bucket ];
    }
}

Image_s Render( const View_e view,
    const std::vector< ByteCell_s >& data, const usize side,
    const Palette_e paletteIndex,
    const BytePlotOptions_s options )
{
    if ( ( side != 256 && side != 512 && side != 1024 ) ||
         data.size() > side * side )
    {
        throw std::invalid_argument( "Invalid render resolution or input size" );
    }

    const Palette_s& palette = Palette_Get( paletteIndex );
    Image_s image = Render_EmptyImage( side, palette );

    switch ( view )
    {
        case CDV_LINEAR:
            Render_ByteRaster( image, data, false, palette,
                Palette_ByteColorTable( paletteIndex ).data() );
            break;

        case CDV_DIGRAPH:
            Render_Digraph( image, data, palette, options );
            break;

        case CDV_HISTOGRAM:
            Render_Histogram( image, data, paletteIndex, options );
            break;

        case CDV_HILBERT:
            Render_ByteRaster( image, data, true, palette,
                Palette_ByteColorTable( paletteIndex ).data() );
            break;

        default:
            throw std::invalid_argument( "Unknown visualization mode" );
    }

    return image;
}

Image_s RenderDetail( const View_e view,
    const std::vector< ByteCell_s >& data, const usize side,
    const Palette_e paletteIndex,
    const BytePlotOptions_s options )
{
    if ( view != CDV_LINEAR && view != CDV_HILBERT )
    {
        return Render( view, data, side, paletteIndex, options );
    }

    Image_s image = Render( view, data, side, paletteIndex, options );

    const Palette_s& palette = Palette_Get( paletteIndex );
    const u32* const byteColors = Palette_ByteColorTable( paletteIndex ).data();

    if ( data.empty() || data.size() == side * side )
    {
        return image;
    }

    const usize pixelCount = side * side;
    const u32* const hilbertPixels = view == CDV_HILBERT
        ? Math_HilbertPixelOffsets( side ).data() : nullptr;

    for ( usize i = 0; i < pixelCount; ++i )
    {
        const usize source = i * data.size() / pixelCount;
        const usize pixel = hilbertPixels ? hilbertPixels[ i ] : i;
        const ByteCell_s& cell = data[ source ];
        image.argb[ pixel ] = cell.valid
            ? byteColors[ cell.value ] : palette.missing;

        image.sourceIndex[ pixel ] = cell.valid
            ? source : CANTORDUST_NO_SOURCE;
    }

    return image;
}

Image_s RenderLinearMapped(
    const std::vector< ByteCell_s >& data,
    const std::vector< usize >& sourceOffsets,
    const usize side, const Palette_e paletteIndex )
{
    if ( ( side != 256 && side != 512 && side != 1024 ) ||
         data.size() > side * side ||
         sourceOffsets.size() > side * side )
    {
        throw std::invalid_argument( "Mapped render input exceeds 65536 bytes" );
    }

    const Palette_s& palette = Palette_Get( paletteIndex );
    Image_s image = Render_EmptyImage( side, palette );
    Render_ByteRasterMapped( image, data, sourceOffsets, palette,
        Palette_ByteColorTable( paletteIndex ).data() );
    return image;
}

Image_s RenderHilbertOverview(
    const std::vector< ByteClassSummary_s >& classes,
    const std::vector< usize >& sourceOffsets,
    const usize side, const Palette_e paletteIndex )
{
    if ( ( side != 256 && side != 512 && side != 1024 ) ||
         classes.size() > side * side ||
         sourceOffsets.size() != classes.size() )
    {
        throw std::invalid_argument( "Overview requires matching samples" );
    }

    const Palette_s& palette = Palette_Get( paletteIndex );
    Image_s image = Render_EmptyImage( side, palette );
    const u32* const hilbertPixels = Math_HilbertPixelOffsets( side ).data();

    for ( usize i = 0; i < classes.size(); ++i )
    {
        const usize pixel = hilbertPixels[ i ];
        image.argb[ pixel ] = Render_ClassColor( classes[ i ], palette );

        if ( classes[ i ].SampleCount() != 0 )
        {
            image.sourceIndex[ pixel ] = sourceOffsets[ i ];
        }
    }

    return image;
}

Image_s RenderOverview(
    const View_e view,
    const std::vector< ByteClassSummary_s >& classes,
    const std::vector< ByteCell_s >& data,
    const std::vector< usize >& sourceOffsets,
    const usize side, const Palette_e paletteIndex,
    const BytePlotOptions_s options )
{
    if ( classes.size() != data.size() ||
         classes.size() != sourceOffsets.size() )
    {
        throw std::invalid_argument( "Overview sample lengths differ" );
    }

    if ( view == CDV_HILBERT )
    {
        return RenderHilbertOverview(
            classes, sourceOffsets, side, paletteIndex );
    }

    if ( view == CDV_LINEAR )
    {
        Image_s image = Render( CDV_LINEAR, data, side, paletteIndex );

        const Palette_s& palette = Palette_Get( paletteIndex );

        for ( usize i = 0; i < classes.size(); ++i )
        {
            image.argb[ i ] = Render_ClassColor( classes[ i ], palette );
            image.sourceIndex[ i ] = classes[ i ].SampleCount() == 0
                ? CANTORDUST_NO_SOURCE : sourceOffsets[ i ];
        }

        return image;
    }

    Image_s image = Render( view, data, side, paletteIndex, options );

    for ( usize& index : image.sourceIndex )
    {
        index = index != CANTORDUST_NO_SOURCE &&
                index < sourceOffsets.size()
            ? sourceOffsets[ index ] : CANTORDUST_NO_SOURCE;
    }

    return image;
}

} // namespace cantordust
