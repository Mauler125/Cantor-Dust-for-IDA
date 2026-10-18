#ifndef MATH_ANOMALY_PALETTE_H
#define MATH_ANOMALY_PALETTE_H

#include "math/anomaly.h"
#include "cantordust/palette.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cantordust
{

namespace anomaly_palette_detail
{

// Find the hue of a sufficiently bright, saturated byte color. Grayscale
// colors (including the Monochrome palette) have no meaningful opposite hue.
inline bool ColorHue( const u32 color, double& hue )
{
    const double red = static_cast< double >( ( color >> 16 ) & 255u ) / 255.0;
    const double green = static_cast< double >( ( color >> 8 ) & 255u ) / 255.0;
    const double blue = static_cast< double >( color & 255u ) / 255.0;
    const double highest = std::max( { red, green, blue } );
    const double lowest = std::min( { red, green, blue } );
    const double range = highest - lowest;
    if ( highest < 0.38 || range < 0.15 || range / highest < 0.22 )
        return false;

    if ( highest == red )
        hue = 60.0 * ( green - blue ) / range;
    else if ( highest == green )
        hue = 120.0 + 60.0 * ( blue - red ) / range;
    else
        hue = 240.0 + 60.0 * ( red - green ) / range;
    if ( hue < 0.0 ) hue += 360.0;
    return true;
}

inline double HueDistance( const double a, const double b )
{
    const double delta = std::abs( a - b );
    return std::min( delta, 360.0 - delta );
}

// Rather than reusing the palette's own colors, choose the hue farthest from
// its three dominant *visible* colors. A Glacier palette therefore yields
// warm markers; Matrix yields purple/pink. This also works for palettes that
// span several opposing hues, where a simple RGB inversion performs poorly.
inline double ContrastHue( const Palette_s& palette )
{
    std::array< double, 3 > hues{};
    usize count = 0;
    for ( const u32 color : { palette.printable, palette.high, palette.histogram } )
    {
        double hue = 0.0;
        if ( ColorHue( color, hue ) )
            hues[ count++ ] = hue;
    }
    if ( count == 0 )
        return 315.0; // Vivid magenta instead of gray on Monochrome.

    double bestHue = 0.0;
    double bestDistance = -1.0;
    for ( int degrees = 0; degrees < 360; ++degrees )
    {
        double distance = 180.0;
        for ( usize i = 0; i < count; ++i )
            distance = std::min( distance,
                HueDistance( static_cast< double >( degrees ), hues[ i ] ) );
        if ( distance > bestDistance )
        {
            bestDistance = distance;
            bestHue = static_cast< double >( degrees );
        }
    }
    return bestHue;
}

inline u32 BrightColor( double hue )
{
    hue = std::fmod( hue + 720.0, 360.0 );
    constexpr double saturation = 0.94;
    constexpr double value = 1.0;
    const double chroma = value * saturation;
    const double step = hue / 60.0;
    const double secondary = chroma * ( 1.0 -
        std::abs( std::fmod( step, 2.0 ) - 1.0 ) );
    const double base = value - chroma;
    double red = 0.0, green = 0.0, blue = 0.0;
    if ( step < 1.0 )      { red = chroma; green = secondary; }
    else if ( step < 2.0 ) { red = secondary; green = chroma; }
    else if ( step < 3.0 ) { green = chroma; blue = secondary; }
    else if ( step < 4.0 ) { green = secondary; blue = chroma; }
    else if ( step < 5.0 ) { red = secondary; blue = chroma; }
    else                  { red = chroma; blue = secondary; }
    const auto channel = [base]( const double value ) {
        return static_cast< u32 >( ( value + base ) * 255.0 + 0.5 );
    };
    return 0xFF000000u | ( channel( red ) << 16 ) |
        ( channel( green ) << 8 ) | channel( blue );
}

} // namespace anomaly_palette_detail

inline u32 Anomaly_PaletteColor( const AnomalyKind_e kind,
    const Palette_e index )
{
    // Keep the five detectors distinct, while grouping them in a contrasting
    // accent family. Precompute once: paintEvent can draw hundreds of markers.
    static const auto colors = [] {
        std::array< std::array< u32, 5 >, CDP_COUNT > table{};
        constexpr std::array< double, 5 > offsets{ 0.0, -26.0, 26.0,
            -52.0, 52.0 };
        for ( usize i = 0; i < Palette_Count(); ++i )
        {
            const double hue = anomaly_palette_detail::ContrastHue(
                Palette_Get( static_cast< Palette_e >( i ) ) );
            for ( usize kindIndex = 0; kindIndex < offsets.size(); ++kindIndex )
                table[ i ][ kindIndex ] = anomaly_palette_detail::BrightColor(
                    hue + offsets[ kindIndex ] );
        }
        return table;
    }();

    const usize paletteIndex = static_cast< unsigned int >( index ) < CDP_COUNT
        ? static_cast< usize >( index ) : 0;
    usize kindIndex = 0;
    switch ( kind )
    {
    case AnomalyKind_e::HighEntropy:      kindIndex = 0; break;
    case AnomalyKind_e::EntropyChange:    kindIndex = 1; break;
    case AnomalyKind_e::Padding:          kindIndex = 2; break;
    case AnomalyKind_e::DominantByte:     kindIndex = 3; break;
    case AnomalyKind_e::RepeatedPattern: kindIndex = 4; break;
    }
    return colors[ paletteIndex ][ kindIndex ];
}

// A light edge against dark bytes, and a dark edge against bright bytes,
// ensures the colored dot remains recognizable on either kind of raster.
inline u32 Anomaly_OutlineColor( const u32 pixel )
{
    const u32 red = ( pixel >> 16 ) & 255u;
    const u32 green = ( pixel >> 8 ) & 255u;
    const u32 blue = pixel & 255u;
    const u32 brightness = ( 299u * red + 587u * green + 114u * blue ) / 1000u;
    return brightness < 145u ? 0xFFFFFFFFu : 0xFF080808u;
}

} // namespace cantordust

#endif // MATH_ANOMALY_PALETTE_H
