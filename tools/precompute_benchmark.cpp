#include "cantordust/renderer.h"
#include "cantordust/cube.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace cantordust;

static std::uint64_t HashImage( const Image_s& image )
{
    std::uint64_t hash = 14695981039346656037ull;
    const auto mix = [&]( std::uint64_t value ) {
        for ( int i = 0; i < 8; ++i )
        {
            hash ^= static_cast< std::uint8_t >( value >> ( i * 8 ) );
            hash *= 1099511628211ull;
        }
    };
    mix( image.width );
    mix( image.height );
    for ( const auto color : image.argb ) mix( color );
    for ( const auto source : image.sourceIndex ) mix( source );
    return hash;
}

template< typename RenderFn >
static void Measure( const char* name, const int iterations, RenderFn render )
{
    // Warm up: the first Hilbert render initializes its lookup table.
    Image_s image = render();
    const std::uint64_t expected = HashImage( image );
    const auto start = std::chrono::steady_clock::now();
    for ( int i = 0; i < iterations; ++i )
        image = render();
    const auto end = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(
        end - start ).count() / iterations;
    std::cout << name << " hash=" << expected << " ms=" << ms << '\n';
}

int main()
{
    std::vector< ByteCell_s > bytes( 1024u * 1024u );
    std::uint32_t seed = 0x51AD2026u;
    for ( auto& cell : bytes )
    {
        seed = seed * 1664525u + 1013904223u;
        cell = { static_cast< u8 >( seed >> 24 ), ( seed % 17u ) != 0 };
    }

    for ( const usize side : { 256u, 512u, 1024u } )
    {
        const std::vector< ByteCell_s > sample(
            bytes.begin(), bytes.begin() + side * side );
        for ( const View_e mode : { CDV_LINEAR, CDV_HILBERT, CDV_DIGRAPH } )
        {
            const std::string name = std::to_string( side ) +
                ( mode == CDV_LINEAR ? " linear" :
                  mode == CDV_HILBERT ? " hilbert" : " digraph" );
            Measure( name.c_str(), side == 1024 ? 4 : 6,
                [&] { return Render( mode, sample, side, CDP_GHIDRA_VIOLET ); } );
        }
    }

    const CubeCloud_s cloud = Cube_BuildCloud(
        std::vector< ByteCell_s >( bytes.begin(), bytes.begin() + 65536 ),
        CDP_GHIDRA_VIOLET );
    CubeCamera_s camera;
    for ( const float size : { 1.0f, 1.45f, 2.0f } )
    {
        CubeRenderOptions_s options;
        options.pointSize = size;
        options.brightness = 1.6f;
        const std::string name = "cube point_size=" + std::to_string( size );
        Measure( name.c_str(), 5,
            [&] { return Cube_Render( cloud, camera, 320, 240,
                CDP_GHIDRA_VIOLET, options ); } );
    }
}
