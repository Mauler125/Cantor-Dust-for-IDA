#include "cantordust/cube.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace cantordust;

static void Tests_Decoding()
{
    assert( Cube_SampleLength( CCV_U8 ) == 4 );
    assert( Cube_SampleLength( CCV_U16_LE ) == 6 );
    assert( Cube_SampleLength( CCV_U32_BE ) == 12 );
    // 3 aligned little-endian u16 values with distinct high bytes.
    const std::uint8_t le16[] = { 0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A };
    CubeTriplet_s t{};
    assert( Cube_DecodeTriplet( le16, CCV_U16_LE, t ) );
    assert( t.x == 0x12 && t.y == 0x56 && t.z == 0x9A );
    assert( t.colorByte == 0x34 );
    assert( Cube_DecodeTriplet( le16, CCV_U16_BE, t ) );
    assert( t.x == 0x34 && t.y == 0x78 && t.z == 0xBC );

    const std::uint8_t le32[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };

    assert( Cube_DecodeTriplet( le32, CCV_U32_LE, t ) );
    assert( t.x == 4 && t.y == 8 && t.z == 12 );
    assert( Cube_DecodeTriplet( le32, CCV_U32_BE, t ) );
    assert( t.x == 1 && t.y == 5 && t.z == 9 );

    const std::uint8_t bytes[] = { 0, 255, 3, 0 };
    assert( Cube_DecodeTriplet( bytes, CCV_POPCOUNT8, t ) );
    assert( t.x == 255 && t.y == 63 && t.z == 0 );
    assert( Cube_DecodeTriplet( bytes, CCV_DELTA8, t ) );
    assert( t.x == 255 && t.y == 4 && t.z == 253 );
    assert( !Cube_DecodeTriplet( nullptr, CCV_U8, t ) );
}

static std::size_t Tests_CountPixels( const Image_s& image,
    const std::uint32_t background )
{
    return static_cast< std::size_t >( std::count_if(
        image.argb.begin(), image.argb.end(),
        [background]( const std::uint32_t c ) { return c != background; } ) );
}

static void Tests_GeometryAndExposure()
{
    CubeCloud_s cloud;
    cloud.points.push_back( { 0.74f, -0.38f, 0.21f,
        14u, 177u, 0xFFE2B58Fu, 0x32u } );

    CubeCamera_s camera;

    const Image_s original = Cube_Render( cloud, camera,
        240, 220, CDP_REFERENCE );

    assert( original.argb == Cube_Render( cloud, camera,
        240, 220, CDP_REFERENCE, {} ).argb );

    CubeRenderOptions_s opts;

    for ( const CubeGeometry_e geometry : { CCG_SPHERE, CCG_CYLINDER,
                                                CCG_TETRAHEDRON } )
    {
        opts.geometry = geometry;

        const Image_s different = Cube_Render( cloud, camera,
            240, 220, CDP_REFERENCE, opts );

        assert( different.argb != original.argb );
        assert( std::find( different.sourceIndex.begin(), different.sourceIndex.end(),
            177u ) != different.sourceIndex.end() );
    }

    opts = {};
    opts.pointSize = 2.0f;

    const Image_s bigger = Cube_Render( cloud, camera,
        240, 220, CDP_REFERENCE, opts );

    assert( Tests_CountPixels( bigger, 0xff000000u ) >=
        Tests_CountPixels( original, 0xff000000u ) );

    assert( bigger.argb != original.argb );

    opts = {};
    opts.brightness = 0.25f;

    const Image_s darker = Cube_Render( cloud, camera,
        240, 220, CDP_REFERENCE, opts );

    opts.brightness = 3.0f;

    const Image_s lighter = Cube_Render( cloud, camera,
        240, 220, CDP_REFERENCE, opts );

    assert( lighter.argb != darker.argb );
    assert( lighter.sourceIndex == darker.sourceIndex );
}

static void Tests_DataBoundaries()
{
    std::vector< ByteCell_s > cells;

    const std::uint8_t data[] = {
        0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A,
        0x01, 0x42, 0x77, 0x55, 0xAA, 0x11, 0x22
    };

    for ( const std::uint8_t byte : data )
        cells.push_back({ byte, true });

    CubeCloud_s cloud = Cube_BuildCloud( cells, CDP_REFERENCE,
        CANTORDUST_CUBE_MAX_POINTS, {}, CCV_U16_LE );

    assert( cloud.acceptedTriplets >= 1 );
    bool found = false;

    for ( const CubePoint_s& point : cloud.points )
        if ( point.sourceIndex == 0 )
            found = true;

    assert( found );
    // No scalar window may cross a segment boundary.
    cloud = Cube_BuildCloud( cells, CDP_REFERENCE,
        CANTORDUST_CUBE_MAX_POINTS, { 5 }, CCV_U16_LE );

    for ( const CubePoint_s& point : cloud.points )
        assert( point.sourceIndex != 0 );

    cells[ 3 ].valid = false;
    cloud = Cube_BuildCloud( cells, CDP_REFERENCE,
        CANTORDUST_CUBE_MAX_POINTS, {}, CCV_U16_LE );

    for ( const CubePoint_s& point : cloud.points )
        assert( point.sourceIndex != 0 );
}

int main()
{
    Tests_Decoding();
    Tests_GeometryAndExposure();
    Tests_DataBoundaries();
    std::cout << "Cube value/geometry/exposure modes passed.\n";
}
