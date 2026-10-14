#include "cantordust/cube.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

using namespace cantordust;

int main( int argc, char** argv )
{
    std::vector< ByteCell_s > cells( 1024 * 1024 );
    std::uint32_t seed = 0xBADC0DEu;

    for ( std::size_t i = 0; i < cells.size(); ++i )
    {
        seed = seed * 1664525u + 1013904223u;
        // Mix structured ASCII and pseudorandom payloads.
        cells[ i ] = { static_cast< std::uint8_t >(
            i % 4 == 0 ? ( seed >> 24 ) : 32 + ( seed % 96 ) ), true };
    }

    const auto start = std::chrono::steady_clock::now();
    const CubeCloud_s cloud = Cube_BuildCloud( cells, CDP_GHIDRA_VIOLET );
    const auto built = std::chrono::steady_clock::now();
    CubeCamera_s camera;
    Image_s frame;
    std::vector< float > depth;

    for ( int i = 0; i < 40; ++i )
    {
        Cube_Rotate( camera, 5.0f, 2.0f );
        Cube_RenderInto( frame, depth, cloud, camera,
            1024, 768, CDP_GHIDRA_VIOLET );
    }

    const auto finished = std::chrono::steady_clock::now();

    const auto buildMs = std::chrono::duration_cast<
        std::chrono::milliseconds >( built - start ).count();

    const auto frameMs = std::chrono::duration_cast<
        std::chrono::milliseconds >( finished - built ).count();

    std::cout << "bytes=" << cells.size()
        << " trigraphs=" << cloud.acceptedTriplets
        << " occupied=" << cloud.occupiedVoxels
        << " drawn=" << cloud.points.size()
        << " build_ms=" << buildMs
        << " avg_frame_ms=" << static_cast< double >( frameMs ) / 40.0
        << '\n';

    seed = 0xA1B2C3D4u;

    for ( ByteCell_s& cell : cells )
    {
        seed = seed * 1664525u + 1013904223u;
        cell = { static_cast< std::uint8_t >( seed >> 24 ), true };
    }

    const auto randomStart = std::chrono::steady_clock::now();
    const CubeCloud_s randomCloud = Cube_BuildCloud(
        cells, CDP_GHIDRA_VIOLET );

    const auto randomBuilt = std::chrono::steady_clock::now();

    for ( int i = 0; i < 40; ++i )
    {
        Cube_Rotate( camera, 5.0f, 2.0f );
        Cube_RenderInto( frame, depth, randomCloud, camera,
            1024, 768, CDP_GHIDRA_VIOLET );
    }

    const auto randomFinished = std::chrono::steady_clock::now();
    std::cout << "random_bytes=" << cells.size()
        << " occupied=" << randomCloud.occupiedVoxels
        << " drawn=" << randomCloud.points.size()
        << " build_ms=" << std::chrono::duration_cast<
            std::chrono::milliseconds >( randomBuilt - randomStart ).count()
        << " avg_frame_ms=" << static_cast< double >(
            std::chrono::duration_cast< std::chrono::milliseconds >(
                randomFinished - randomBuilt ).count() ) / 40.0 << '\n';

    if ( argc > 1 )
    {
        std::ofstream out( argv[ 1 ], std::ios::binary );
        out << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";

        for ( const std::uint32_t color : frame.argb )
        {
            const char rgb[ 3 ] = {
                static_cast< char >( ( color >> 16 ) & 0xFF ),
                static_cast< char >( ( color >> 8 ) & 0xFF ),
                static_cast< char >( color & 0xFF )
            };
            out.write( rgb, sizeof( rgb ) );
        }
    }

    return 0;
}
