#include "cantordust/cube.h"
#include "math/cube_lighting.h"
#include "math/cube.h"

#ifdef NDEBUG
    #undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <cmath>
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

static void Tests_CenteredRegularTetrahedron()
{
    constexpr float tolerance = 0.00002f;

    // The origin is the inside center of the cloud, not a corner or base.
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Math_CubeMapTetrahedron( x, y, z );
    assert( x == 0.0f && y == 0.0f && z == 0.0f );

    const auto& vertices = Math_CubeTetrahedronVertices();
    const std::array< std::array< float, 3 >, 4 > cubeCorners = {{
        {{ 1.0f, 1.0f, 1.0f }}, {{ 1.0f, -1.0f, -1.0f }},
        {{ -1.0f, 1.0f, -1.0f }}, {{ -1.0f, -1.0f, 1.0f }}
    }};

    // Shared vertices must match the actual byte-cloud mapping.
    for ( std::size_t i = 0; i < vertices.size(); ++i )
    {
        auto corner = cubeCorners[ i ];
        Math_CubeMapTetrahedron( corner[ 0 ], corner[ 1 ], corner[ 2 ] );
        for ( std::size_t axis = 0; axis < 3; ++axis )
            assert( std::abs( corner[ axis ] - vertices[ i ][ axis ] ) < tolerance );
    }

    // Exactly one upright apex and three coplanar base vertices.
    assert( std::abs( vertices[ 0 ][ 0 ] ) < tolerance );
    assert( std::abs( vertices[ 0 ][ 1 ] - 1.0f ) < tolerance );
    assert( std::abs( vertices[ 0 ][ 2 ] ) < tolerance );

    for ( std::size_t i = 1; i < vertices.size(); ++i )
        assert( std::abs( vertices[ i ][ 1 ] + 1.0f / 3.0f ) < tolerance );

    for ( std::size_t axis = 0; axis < 3; ++axis )
    {
        float centroid = 0.0f;
        for ( const auto& vertex : vertices )
            centroid += vertex[ axis ];
        assert( std::abs( centroid ) < tolerance );
    }

    constexpr float expectedEdgeSquared = 8.0f / 3.0f;
    for ( std::size_t a = 0; a < vertices.size(); ++a )
        for ( std::size_t b = a + 1; b < vertices.size(); ++b )
        {
            float edgeSquared = 0.0f;
            for ( std::size_t axis = 0; axis < 3; ++axis )
            {
                const float delta = vertices[ a ][ axis ] - vertices[ b ][ axis ];
                edgeSquared += delta * delta;
            }
            assert( std::abs( edgeSquared - expectedEdgeSquared ) < tolerance );
        }

    // Test the entire mapped volume, not just its boundary vertices.
    // A regular tetrahedron with unit circumradius satisfies these planes.
    for ( int ix = -8; ix <= 8; ++ix )
        for ( int iy = -8; iy <= 8; ++iy )
            for ( int iz = -8; iz <= 8; ++iz )
            {
                float px = ix / 8.0f;
                float py = iy / 8.0f;
                float pz = iz / 8.0f;
                Math_CubeMapTetrahedron( px, py, pz );

                for ( const auto& vertex : vertices )
                {
                    const float dot = px * vertex[ 0 ] +
                        py * vertex[ 1 ] + pz * vertex[ 2 ];
                    assert( dot >= -1.0f / 3.0f - tolerance );
                }
            }

    // A cloud point at the origin must remain at the viewport center.
    CubeCloud_s centered;
    centered.points.push_back( { 0.0f, 0.0f, 0.0f,
        14u, 177u, 0xFFE2B58Fu, 0x32u } );
    CubeRenderOptions_s options;
    options.geometry = CCG_TETRAHEDRON;
    const Image_s frame = Cube_Render( centered, {},
        240, 220, CDP_REFERENCE, options );
    assert( frame.SourceAt( 120, 110 ) == 177u );
}

int main()
{
    Tests_CenteredRegularTetrahedron();
    Tests_MathAxisColors();
    Tests_ScratchFrameEquivalence();
    std::cout << "Cube renderer equivalence tests passed.\n";
    return 0;
}
