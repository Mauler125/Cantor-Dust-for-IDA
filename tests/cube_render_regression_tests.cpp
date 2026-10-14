#include "cantordust/cube.h"
#include "math/cube_lighting.h"

#ifdef NDEBUG
    #undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace cantordust;

static CubeCloud_s Tests_BuildCloud()
{
    std::vector< ByteCell_s > cells( 32768 );
    std::uint32_t seed = 0xA17C2026u;

    for ( ByteCell_s& cell : cells )
    {
        seed = seed * 1664525u + 1013904223u;
        cell = { static_cast< std::uint8_t >( seed >> 24 ), true };
    }

    return Cube_BuildCloud( cells, CDP_GHIDRA_VIOLET );
}

static void Tests_MathAxisColors()
{
    for ( std::size_t i = 0; i < Palette_Count(); ++i )
    {
        const Palette_e palette = static_cast< Palette_e >( i );
        const MathCubeAxes_s axes = Math_CubeAxes( palette );

        for ( const std::uint32_t byte : { 0u, 1u, 64u, 128u, 255u } )
        {
            const MathColor_s a = Math_CubeAxisColor(
                byte, 128u, 255u - byte, palette );

            const MathColor_s b = Math_CubeAxisColor(
                byte, 128u, 255u - byte, axes );

            assert( a.red == b.red );
            assert( a.green == b.green );
            assert( a.blue == b.blue );
        }
    }
}

static void Tests_ScratchFrameEquivalence()
{
    const CubeCloud_s cloud = Tests_BuildCloud();
    CubeRenderScratch_s scratch;
    Image_s reused;
    std::vector< float > reusedDepth;

    for ( const Palette_e palette : {
             CDP_REFERENCE, CDP_GHIDRA_VIOLET, CDP_AURORA } )
    {
        for ( const float zoom : { 0.35f, 1.0f, 4.0f } )
        {
            CubeCamera_s camera;
            camera.zoom = zoom;

            const Image_s reference = Cube_Render(
                cloud, camera, 320, 240, palette );

            Cube_RenderInto( reused, reusedDepth, scratch,
                cloud, camera, 320, 240, palette );

            assert( reused.argb == reference.argb );
            assert( reused.sourceIndex == reference.sourceIndex );
            assert( reusedDepth.size() == reused.argb.size() );

            const float* const oldBuffer = scratch.red.data();
            Cube_RenderInto( reused, reusedDepth, scratch,
                cloud, camera, 320, 240, palette );

            assert( scratch.red.data() == oldBuffer );
            assert( reused.argb == reference.argb );
            assert( reused.sourceIndex == reference.sourceIndex );
        }
    }

    Cube_RenderInto( reused, reusedDepth, scratch,
        cloud, {}, 0, 240, CDP_REFERENCE );

    assert( reused.argb.empty() );
    assert( scratch.red.empty() );
    assert( reusedDepth.empty() );
}

int main()
{
    Tests_MathAxisColors();
    Tests_ScratchFrameEquivalence();
    std::cout << "Cube renderer equivalence tests passed.\n";
    return 0;
}
