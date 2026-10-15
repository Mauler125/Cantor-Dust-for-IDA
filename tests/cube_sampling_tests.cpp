#include "cantordust/cube.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using namespace cantordust;

static std::vector< ByteCell_s > Cells( const std::vector< u8 >& bytes )
{
    std::vector< ByteCell_s > cells;
    for ( const u8 byte : bytes )
        cells.push_back({ byte, true });
    return cells;
}

static void WriteFloat( std::vector< u8 >& out, const float value,
    const bool bigEndian )
{
    u32 raw = 0;
    static_assert( sizeof( raw ) == sizeof( value ), "Requires Float32" );
    std::memcpy( &raw, &value, sizeof( raw ) );
    for ( unsigned int i = 0; i < 4; ++i )
        out.push_back( static_cast< u8 >(
            raw >> ( 8u * ( bigEndian ? 3u - i : i ) ) ) );
}

static void Tests_NewByteModes()
{
    CubeTriplet_s triplet{};
    const u8 bytes[] = { 0x20, 0x10, 0x50, 0x50 };
    assert( Cube_DecodeTriplet( bytes, CCV_XOR8, triplet ) );
    assert( triplet.x == 0x30 && triplet.y == 0x40 && triplet.z == 0 );
    assert( Cube_DecodeTriplet( bytes, CCV_SIGNED_DELTA8, triplet ) );
    assert( triplet.x == 119 && triplet.y == 159 && triplet.z == 127 );

    const u8 nibble[] = { 0, 0xA7, 0x3F, 0x00 };
    assert( Cube_DecodeTriplet( nibble, CCV_NIBBLE8, triplet ) );
    assert( triplet.x == 170 && triplet.y == 119 && triplet.z == 51 );

    const u8 bits[] = { 0, 1, 2, 0x80 };
    assert( Cube_DecodeTriplet( bits, CCV_BIT_PLANE, triplet, 0 ) );
    assert( triplet.x == 255 && triplet.y == 0 && triplet.z == 0 );
    assert( Cube_DecodeTriplet( bits, CCV_BIT_PLANE, triplet, 7 ) );
    assert( triplet.x == 0 && triplet.y == 0 && triplet.z == 255 );
    assert( !Cube_DecodeTriplet( bits, CCV_BIT_PLANE, triplet, 8 ) );
}

static void Tests_IntegerWidths()
{
    CubeTriplet_s triplet{};
    std::array< u8, 24 > bytes{};
    for ( unsigned int i = 0; i < bytes.size(); ++i )
        bytes[ i ] = static_cast< u8 >( i + 1u );

    struct Expected_s { CubeValueMode_e mode; u8 x, y, z; usize length, stride; };
    const Expected_s expected[] = {
        { CCV_U16_LE, 2, 4, 6, 6, 2 },
        { CCV_U16_BE, 1, 3, 5, 6, 2 },
        { CCV_U16_LE_LOW, 1, 3, 5, 6, 2 },
        { CCV_U16_BE_LOW, 2, 4, 6, 6, 2 },
        { CCV_U32_LE, 4, 8, 12, 12, 4 },
        { CCV_U32_BE, 1, 5, 9, 12, 4 },
        { CCV_U32_LE_LOW, 1, 5, 9, 12, 4 },
        { CCV_U32_BE_LOW, 4, 8, 12, 12, 4 },
        { CCV_U64_LE, 8, 16, 24, 24, 8 },
        { CCV_U64_BE, 1, 9, 17, 24, 8 },
        { CCV_U64_LE_LOW, 1, 9, 17, 24, 8 },
        { CCV_U64_BE_LOW, 8, 16, 24, 24, 8 },
        { CCV_FLOAT32_LE, 0, 0, 0, 12, 4 },
        { CCV_FLOAT32_BE, 0, 0, 0, 12, 4 }
    };
    for ( const Expected_s& test : expected )
    {
        assert( Cube_SampleLength( test.mode ) == test.length );
        assert( Cube_DefaultStride( test.mode ) == test.stride );
        if ( Cube_IsFloatMode( test.mode ) )
            continue;
        assert( Cube_DecodeTriplet( bytes.data(), test.mode, triplet ) );
        assert( triplet.x == test.x && triplet.y == test.y &&
            triplet.z == test.z && triplet.colorByte == 1u );
    }
}

static void Tests_FloatModes()
{
    for ( const bool big : { false, true } )
    {
        const CubeValueMode_e mode = big ? CCV_FLOAT32_BE : CCV_FLOAT32_LE;
        std::vector< u8 > bytes;
        for ( const float value : { 1.0f, -2.0f, 4.0f })
            WriteFloat( bytes, value, big );
        CubeTriplet_s result;
        assert( Cube_DecodeTriplet( bytes.data(), mode, result ) );
        assert( result.x == 255 && result.y == 0 && result.z == 255 );

        f32 x = 0, y = 0, z = 0;
        assert( Cube_DecodeFloatTriplet( bytes.data(), mode, x, y, z ) );
        assert( x == 1.0f && y == -2.0f && z == 4.0f );

        std::vector< u8 > second;
        for ( const float value : { -1.0f, 2.0f, -4.0f })
            WriteFloat( second, value, big );
        bytes.insert( bytes.end(), second.begin(), second.end() );
        const CubeCloud_s cloud = Cube_BuildCloud( Cells( bytes ),
            CDP_REFERENCE, CANTORDUST_CUBE_MAX_POINTS, {}, mode, 12 );
        assert( cloud.acceptedTriplets == 2 );
        assert( cloud.occupiedVoxels == 2 );
        assert( !cloud.points.empty() );
        for ( const CubePoint_s& point : cloud.points )
            assert( point.sourceIndex == 0 || point.sourceIndex == 12 );

        std::vector< u8 > nanBytes;
        for ( const float value : { NAN, 1.0f, 2.0f })
            WriteFloat( nanBytes, value, big );
        assert( !Cube_DecodeTriplet( nanBytes.data(), mode, result ) );
        const CubeCloud_s bad = Cube_BuildCloud(
            Cells( nanBytes ), CDP_REFERENCE, CANTORDUST_CUBE_MAX_POINTS,
            {}, mode );
        assert( bad.acceptedTriplets == 0 );
    }
}

static void Tests_StrideAndAlignment()
{
    assert( Cube_FirstAligned( 11, 4, 2 ) == 14 );
    assert( Cube_FirstAligned( 20, 4, 2 ) == 22 );
    assert( Cube_FirstAligned( 6, 4, 6 ) == 6 );
    assert( Cube_FirstAligned( UINT64_MAX - 1, 4, 3 ) == UINT64_MAX );

    std::vector< u8 > bytes( 40 );
    for ( usize i = 0; i < bytes.size(); ++i )
        bytes[ i ] = static_cast< u8 >( i );

    // The actual input starts at logical offset 10. Stride 4 and alignment
    // 2 should choose byte 10, 14, 18, etc., never rebase to local zero.
    const CubeCloud_s cloud = Cube_BuildCloud(
        Cells( bytes ), CDP_REFERENCE, CANTORDUST_CUBE_MAX_POINTS,
        { 13 }, CCV_U8, 4, 2, 0, 10 );
    assert( cloud.acceptedTriplets == 9 ); // The sample crossing 13 is omitted.

    const CubeCloud_s noBoundary = Cube_BuildCloud(
        Cells( bytes ), CDP_REFERENCE, CANTORDUST_CUBE_MAX_POINTS,
        {}, CCV_U8, 4, 2, 0, 10 );
    assert( noBoundary.acceptedTriplets == 10 );

    const CubeCloud_s defaultStep = Cube_BuildCloud(
        Cells( bytes ), CDP_REFERENCE, CANTORDUST_CUBE_MAX_POINTS,
        {}, CCV_U8 );
    assert( defaultStep.acceptedTriplets == 37 );

    // All picking metadata must still point to a valid sample anchor.
    for ( const CubePoint_s& point : cloud.points )
    {
        assert( point.sourceIndex % 4 == 0 );
        assert( point.sourceIndex < bytes.size() );
    }
}

int main()
{
    Tests_NewByteModes();
    Tests_IntegerWidths();
    Tests_FloatModes();
    Tests_StrideAndAlignment();
    std::cout << "Extended 3D Byte Cloud decoders and sampling passed.\n";
}
