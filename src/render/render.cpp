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
    const Palette_s& palette )
{
    std::array< u32, CANTORDUST_WINDOW_SIZE > counts{};
    std::array< usize, CANTORDUST_WINDOW_SIZE > first{};
    first.fill( CANTORDUST_NO_SOURCE );

    const ByteCell_s* const cells = data.data();

    for ( usize i = 0; i + 1 < data.size(); ++i )
    {
        // Skip byte pairs crossing unloaded IDA memory.
        if ( V_UNLIKELY( !cells[ i ].valid || !cells[ i + 1 ].valid ) )
        {
            continue;
        }

        const usize pixel =
            ( 255u - cells[ i + 1 ].value ) * CANTORDUST_SIDE + cells[ i ].value;

        ++counts[ pixel ];

        if ( first[ pixel ] == CANTORDUST_NO_SOURCE )
        {
            first[ pixel ] = i;
        }
    }

    const u32 highest =
        *std::max_element( counts.begin(), counts.end() );

    // At 256 pixels per side, every bucket is visited once. At higher
    // resolutions the same bucket is used repeatedly, so cache its exact
    // color once per frame instead of repeating the density logarithm.
    if ( image.width == CANTORDUST_SIDE &&
         image.height == CANTORDUST_SIDE )
    {
        for ( usize pixel = 0; pixel < counts.size(); ++pixel )
        {
            image.argb[ pixel ] = Render_DensityColor(
                counts[ pixel ], highest, palette );
            image.sourceIndex[ pixel ] = first[ pixel ];
        }
        return;
    }

    std::vector< u32 > bucketColors( CANTORDUST_WINDOW_SIZE );
    for ( usize bucket = 0; bucket < bucketColors.size(); ++bucket )
        bucketColors[ bucket ] = Render_DensityColor(
            counts[ bucket ], highest, palette );

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
    const Palette_s& palette )
{
    std::array< u32, CANTORDUST_SIDE > counts{};
    std::array< usize, CANTORDUST_SIDE > first{};
    first.fill( CANTORDUST_NO_SOURCE );

    for ( usize i = 0; i < data.size(); ++i )
    {
        const ByteCell_s& cell = data[ i ];

        if ( V_UNLIKELY( !cell.valid ) )
        {
            continue;
        }

        ++counts[ cell.value ];

        if ( first[ cell.value ] == CANTORDUST_NO_SOURCE )
        {
            first[ cell.value ] = i;
        }
    }

    const u32 highest =
        *std::max_element( counts.begin(), counts.end() );

    if ( highest == 0 )
    {
        return;
    }

    for ( usize x = 0; x < image.width; ++x )
    {
        const usize bucket = x * CANTORDUST_SIDE / image.width;

        if ( counts[ bucket ] == 0 )
        {
            continue;
        }

        const usize height = Math_HistogramHeight(
            counts[ bucket ], highest, image.height - 1 );

        for ( usize y = image.height - height;
              y < image.height; ++y )
        {
            const usize pixel = y * image.width + x;
            image.argb[ pixel ] = palette.histogram;
            image.sourceIndex[ pixel ] = first[ bucket ];
        }
    }
}

Image_s Render( const View_e view,
    const std::vector< ByteCell_s >& data, const usize side,
    const Palette_e paletteIndex )
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
            Render_Digraph( image, data, palette );
            break;

        case CDV_HISTOGRAM:
            Render_Histogram( image, data, palette );
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
    const Palette_e paletteIndex )
{
    if ( view != CDV_LINEAR && view != CDV_HILBERT )
    {
        return Render( view, data, side, paletteIndex );
    }

    Image_s image = Render( view, data, side, paletteIndex );

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
    const usize side, const Palette_e paletteIndex )
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

    Image_s image = Render( view, data, side, paletteIndex );

    for ( usize& index : image.sourceIndex )
    {
        index = index != CANTORDUST_NO_SOURCE &&
                index < sourceOffsets.size()
            ? sourceOffsets[ index ] : CANTORDUST_NO_SOURCE;
    }

    return image;
}

} // namespace cantordust
