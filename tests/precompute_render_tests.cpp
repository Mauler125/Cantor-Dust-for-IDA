#include "cantordust/renderer.h"
#include "cantordust/cube.h"
#include "math/hilbert.h"
#include "math/color.h"
#include "math/density.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace cantordust;

static void Test_PaletteTables()
{
    for ( unsigned int p = 0; p < Palette_Count(); ++p )
    {
        const Palette_e index = static_cast< Palette_e >( p );
        const auto& table = Palette_ByteColorTable( index );
        for ( unsigned int byte = 0; byte < 256; ++byte )
            assert( table[ byte ] == Palette_ByteColor(
                static_cast< u8 >( byte ), Palette_Get( index ) ) );
    }

    const auto& fallback = Palette_ByteColorTable( CDP_REFERENCE );
    assert( Palette_ByteColorTable( static_cast< Palette_e >( 777 ) ) == fallback );
}

static void Test_PopcountTable()
{
    for ( unsigned int byte = 0; byte < 256; ++byte )
    {
        const u8 values[] = { 13, static_cast< u8 >( byte ),
            static_cast< u8 >( 255u - byte ), 0xff };
        CubeTriplet_s result;
        assert( Cube_DecodeTriplet( values, CCV_POPCOUNT8, result ) );

        auto reference = []( unsigned int bits ) {
            unsigned int count = 0;
            while ( bits )
            {
                count += bits & 1u;
                bits >>= 1u;
            }
            return static_cast< u8 >( count * 255u / 8u );
        };

        assert( result.x == reference( byte ) );
        assert( result.y == reference( 255u - byte ) );
        assert( result.z == 255 );
    }
}

static void Test_HilbertAndLinear()
{
    for ( const usize side : { 256u, 512u, 1024u } )
    {
        std::vector< ByteCell_s > cells( side * side );
        std::vector< usize > offsets( cells.size() );
        for ( usize i = 0; i < cells.size(); ++i )
        {
            cells[ i ] = { static_cast< u8 >( ( i * 13 + 7 ) & 255u ),
                           i % 13 != 0 };
            offsets[ i ] = i + 1234u;
        }

        const Image_s linear = Render( CDV_LINEAR, cells, side, CDP_AURORA );
        const Image_s hilbert = Render( CDV_HILBERT, cells, side, CDP_AURORA );
        const Image_s detail = RenderDetail(
            CDV_HILBERT, std::vector< ByteCell_s >( cells.begin(),
                cells.begin() + 251u ), side, CDP_AURORA );

        // Every byte remains uniquely reachable through the cached mapping.
        for ( usize i = 0; i < cells.size(); ++i )
        {
            const Point_s p = Math_HilbertPosition( i, side );
            const usize index = p.y * side + p.x;
            assert( Math_HilbertPixelOffsets( side )[ i ] == index );
            const u32 expected = cells[ i ].valid
                ? Palette_ByteColor( cells[ i ].value, Palette_Get( CDP_AURORA ) )
                : Palette_Get( CDP_AURORA ).missing;
            assert( hilbert.argb[ index ] == expected );
            assert( linear.argb[ i ] == expected );
            assert( hilbert.sourceIndex[ index ] ==
                ( cells[ i ].valid ? i : CANTORDUST_NO_SOURCE ) );
        }

        for ( usize i = 0; i < side * side; i += 101 )
        {
            const Point_s p = Math_HilbertPosition( i, side );
            const usize source = i * 251u / ( side * side );
            const usize pixel = p.y * side + p.x;
            assert( detail.sourceIndex[ pixel ] ==
                ( cells[ source ].valid ? source : CANTORDUST_NO_SOURCE ) );
        }
    }
}

static void Test_DigraphDensity()
{
    const usize side = 512;
    std::vector< ByteCell_s > cells( 65536 );
    std::array< u32, 65536 > counts{};
    for ( usize i = 0; i < cells.size(); ++i )
        cells[ i ] = { static_cast< u8 >( ( i * 53u + i / 7u ) & 255u ),
            i % 17 != 0 };
    for ( usize i = 0; i + 1 < cells.size(); ++i )
        if ( cells[ i ].valid && cells[ i + 1 ].valid )
            ++counts[ ( 255u - cells[ i + 1 ].value ) * 256u + cells[ i ].value ];

    const u32 highest = *std::max_element( counts.begin(), counts.end() );
    const auto& palette = Palette_Get( CDP_GHIDRA_VIOLET );
    const Image_s image = Render( CDV_DIGRAPH, cells, side, CDP_GHIDRA_VIOLET );
    for ( usize y = 0; y < side; ++y )
    {
        for ( usize x = 0; x < side; ++x )
        {
            const usize bucket = ( y / 2 ) * 256u + x / 2;
            const u32 expected = counts[ bucket ] == 0
                ? palette.background
                : Math_BlendColor( palette.densityLow, palette.densityHigh,
                    Math_LogDensity( counts[ bucket ], highest ) );
            assert( image.argb[ y * side + x ] == expected );
        }
    }
}

int main()
{
    Test_PaletteTables();
    Test_PopcountTable();
    Test_HilbertAndLinear();
    Test_DigraphDensity();
    std::cout << "Precomputed lookup equivalence tests passed.\n";
}
