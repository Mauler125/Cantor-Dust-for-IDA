#include "math/anomaly.h"
#include "math/anomaly_palette.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <random>
#include <vector>
#include <algorithm>

using namespace cantordust;

static std::vector< ByteCell_s > Filled( const usize count, const u8 byte )
{
    return std::vector< ByteCell_s >( count, { byte, true } );
}
static bool Has( const AnomalyDetector& detector, const AnomalyKind_e kind )
{
    const auto& items = detector.Findings();
    return std::any_of( items.begin(), items.end(), [kind]( const auto& item ) {
        return item.kind == kind;
    } );
}
int main()
{
    // Every palette receives a bright family of five distinct anomaly hues,
    // deliberately unlike the byte colors rather than reusing them.
    for ( usize i = 0; i < Palette_Count(); ++i )
    {
        const auto mode = static_cast< Palette_e >( i );
        const Palette_s& palette = Palette_Get( mode );
        const std::array< AnomalyKind_e, 5 > kinds{
            AnomalyKind_e::HighEntropy, AnomalyKind_e::EntropyChange,
            AnomalyKind_e::Padding, AnomalyKind_e::DominantByte,
            AnomalyKind_e::RepeatedPattern };
        std::vector< u32 > colors;
        for ( const auto kind : kinds )
        {
            const u32 color = Anomaly_PaletteColor( kind, mode );
            assert( ( color >> 24 ) == 0xFFu );
            assert( std::find( colors.begin(), colors.end(), color ) ==
                colors.end() );
            colors.push_back( color );

            const u32 red = ( color >> 16 ) & 255u;
            const u32 green = ( color >> 8 ) & 255u;
            const u32 blue = color & 255u;
            assert( std::max( { red, green, blue } ) >= 245u );
            assert( std::max( { red, green, blue } ) -
                std::min( { red, green, blue } ) >= 200u );
        }
        // The primary marker should be the furthest hue from the most
        // common visible palette colors, not equal to any of them.
        double accentHue = 0.0;
        assert( anomaly_palette_detail::ColorHue( colors[ 0 ], accentHue ) );
        for ( const u32 base :
            { palette.printable, palette.high, palette.histogram } )
        {
            double baseHue = 0.0;
            if ( anomaly_palette_detail::ColorHue( base, baseHue ) )
                assert( anomaly_palette_detail::HueDistance(
                    accentHue, baseHue ) >= 45.0 );
        }
    }
    // Glacier markers must be warm/vivid rather than glacier blue.
    for ( const auto kind : { AnomalyKind_e::HighEntropy,
        AnomalyKind_e::EntropyChange, AnomalyKind_e::Padding,
        AnomalyKind_e::DominantByte, AnomalyKind_e::RepeatedPattern } )
    {
        const u32 color = Anomaly_PaletteColor( kind, CDP_GLACIER );
        assert( ( ( color >> 16 ) & 255u ) > ( color & 255u ) );
    }
    assert( Anomaly_PaletteColor( AnomalyKind_e::Padding, CDP_REFERENCE ) !=
        Anomaly_PaletteColor( AnomalyKind_e::Padding, CDP_GLACIER ) );
    // Outlines switch from white to near-black depending on local raster
    // brightness. They are independent from the selected color palette.
    assert( Anomaly_OutlineColor( 0xFF000000u ) == 0xFFFFFFFFu );
    assert( Anomaly_OutlineColor( 0xFF101C2Fu ) == 0xFFFFFFFFu );
    assert( Anomaly_OutlineColor( 0xFFFFFFFFu ) == 0xFF080808u );
    assert( Anomaly_OutlineColor( 0xFFFFFF00u ) == 0xFF080808u );

    constexpr usize block = 4096;
    AnomalyDetector padding;
    padding.Analyze( Filled( block, 0 ), 0 );
    padding.Analyze( Filled( block, 0 ), block );
    assert( Has( padding, AnomalyKind_e::Padding ) );
    assert( padding.Findings().front().length == 2 * block );

    AnomalyDetector ff;
    ff.Analyze( Filled( block, 255 ), 0 );
    assert( Has( ff, AnomalyKind_e::Padding ) );

    AnomalyDetector pattern;
    auto alternating = Filled( block, 0 );
    for ( usize i = 0; i < block; ++i )
        alternating[ i ].value = static_cast< u8 >( ( i % 2 ) ? 0x13 : 0xab );
    pattern.Analyze( alternating, 0 );
    assert( Has( pattern, AnomalyKind_e::RepeatedPattern ) );
    assert( !Has( pattern, AnomalyKind_e::Padding ) );

    std::mt19937 rng( 0xCA17 );
    auto noise = Filled( block, 0 );
    for ( auto& cell : noise ) cell.value = static_cast< u8 >( rng() & 255 );
    const auto validNoise = noise;
    AnomalyDetector entropy;
    entropy.Analyze( Filled( block, 0 ), 0 );
    entropy.Analyze( noise, block );
    assert( Has( entropy, AnomalyKind_e::HighEntropy ) );
    assert( Has( entropy, AnomalyKind_e::EntropyChange ) );

    AnomalyDetector noJoin;
    noJoin.Analyze( Filled( block, 0 ), 0 );
    noJoin.Break();
    noJoin.Analyze( noise, block );
    assert( !Has( noJoin, AnomalyKind_e::EntropyChange ) );

    AnomalyDetector invalid;
    noise[ 0 ].valid = false;
    noise[ 1 ].valid = false;
    for ( usize i = 2; i < 1024; ++i ) noise[ i ].valid = false;
    invalid.Analyze( noise, 0 );
    assert( invalid.Findings().empty() );

    AnomalyDetector shortBlock;
    shortBlock.Analyze( Filled( 100, 0 ), 0 );
    assert( shortBlock.Findings().empty() );

    // Defaults stay compatible with the original on-demand scanner.
    AnomalyOptions_s defaults;
    assert( defaults.highEntropy && defaults.entropyChange &&
        defaults.padding && defaults.dominantByte && defaults.repeatedPattern );
    assert( defaults.highEntropyBits == 7.55 );
    assert( defaults.entropyChangeBits == 1.5 );
    assert( defaults.paddingFraction == 0.93 );
    assert( defaults.dominantFraction == 0.70 );
    assert( defaults.patternFraction == 0.94 );
    assert( defaults.windowBytes == 4096 && defaults.scanBudgetMiB == 16 );
    assert( defaults.maxFindingsPerKind == 100 );

    // Independently enabled detectors, including zeros becoming dominant
    // if the dedicated padding detector is switched off.
    AnomalyOptions_s disabled = defaults;
    disabled.padding = false;
    disabled.repeatedPattern = false;
    AnomalyDetector noPadding( disabled );
    noPadding.Analyze( Filled( block, 0 ), 0 );
    assert( !Has( noPadding, AnomalyKind_e::Padding ) );
    assert( Has( noPadding, AnomalyKind_e::DominantByte ) );

    disabled.dominantByte = false;
    disabled.highEntropy = false;
    disabled.entropyChange = false;
    AnomalyDetector nothing( disabled );
    nothing.Analyze( validNoise, 0 );
    nothing.Analyze( Filled( block, 0 ), block );
    assert( nothing.Findings().empty() );

    AnomalyOptions_s sensitive = defaults;
    sensitive.highEntropyBits = 8.0;
    AnomalyDetector strictEntropy( sensitive );
    strictEntropy.Analyze( validNoise, 0 );
    assert( !Has( strictEntropy, AnomalyKind_e::HighEntropy ) );
    sensitive.highEntropyBits = 7.0;
    AnomalyDetector looseEntropy( sensitive );
    looseEntropy.Analyze( validNoise, 0 );
    assert( Has( looseEntropy, AnomalyKind_e::HighEntropy ) );

    sensitive = defaults;
    sensitive.entropyChangeBits = 8.0;
    AnomalyDetector strictJump( sensitive );
    strictJump.Analyze( Filled( block, 0 ), 0 );
    strictJump.Analyze( validNoise, block );
    assert( !Has( strictJump, AnomalyKind_e::EntropyChange ) );
    sensitive.entropyChangeBits = 1.0;
    AnomalyDetector looseJump( sensitive );
    looseJump.Analyze( Filled( block, 0 ), 0 );
    looseJump.Analyze( validNoise, block );
    assert( Has( looseJump, AnomalyKind_e::EntropyChange ) );

    auto mixed = Filled( block, 0 );
    for ( usize i = 0; i < 500; ++i ) mixed[ i ].value = 0x7c;
    sensitive = defaults;
    sensitive.paddingFraction = 0.90; // ~87.8% zeros should not pass
    sensitive.dominantByte = false;
    AnomalyDetector strictPadding( sensitive );
    strictPadding.Analyze( mixed, 0 );
    assert( !Has( strictPadding, AnomalyKind_e::Padding ) );
    sensitive.paddingFraction = 0.85;
    AnomalyDetector loosePadding( sensitive );
    loosePadding.Analyze( mixed, 0 );
    assert( Has( loosePadding, AnomalyKind_e::Padding ) );

    sensitive = defaults;
    sensitive.padding = false;
    sensitive.dominantFraction = 0.90;
    AnomalyDetector strictDominant( sensitive );
    strictDominant.Analyze( mixed, 0 );
    assert( !Has( strictDominant, AnomalyKind_e::DominantByte ) );
    sensitive.dominantFraction = 0.80;
    AnomalyDetector looseDominant( sensitive );
    looseDominant.Analyze( mixed, 0 );
    assert( Has( looseDominant, AnomalyKind_e::DominantByte ) );

    auto imperfectPattern = alternating;
    for ( usize i = 0; i < block; i += 31 )
        imperfectPattern[ i ].value ^= 0x55;
    sensitive = defaults;
    sensitive.patternFraction = 0.98;
    AnomalyDetector strictPattern( sensitive );
    strictPattern.Analyze( imperfectPattern, 0 );
    assert( !Has( strictPattern, AnomalyKind_e::RepeatedPattern ) );
    sensitive.patternFraction = 0.85;
    AnomalyDetector loosePattern( sensitive );
    loosePattern.Analyze( imperfectPattern, 0 );
    assert( Has( loosePattern, AnomalyKind_e::RepeatedPattern ) );

    // Window-size selection is respected by the core scanner, and a run
    // of adjacent matching windows still coalesces correctly.
    AnomalyDetector smallWindows;
    smallWindows.Analyze( Filled( 1024, 0 ), 0 );
    smallWindows.Analyze( Filled( 1024, 0 ), 1024 );
    assert( Has( smallWindows, AnomalyKind_e::Padding ) );
    assert( smallWindows.Findings().front().length == 2048 );
    AnomalyDetector largeWindow;
    largeWindow.Analyze( Filled( 16384, 0xff ), 0 );
    assert( Has( largeWindow, AnomalyKind_e::Padding ) );
}
