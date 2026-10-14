#include "cantordust/universe.h"

#include <chrono>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace cantordust;

int main( int argc, char** argv )
{
    const char* const path = argc > 1 ? argv[ 1 ] : "universe.ppm";
    std::vector< UniverseFunction_s > functions;
    std::vector< UniverseReference_s > references;

    for ( std::uint32_t i = 0; i < 900; ++i )
    {
        functions.push_back({ 0x100000u + i * 0x100u,
            "function_" + std::to_string( i ) });
    }

    std::uint32_t seed = 0xAE125682u;

    for ( std::uint32_t i = 1; i < 900; ++i )
    {
        seed = seed * 1664525u + 1013904223u;

        const std::uint32_t parent = seed % std::min( i, 80u );
        references.push_back({ functions[ i ].address,
            functions[ parent ].address });

        if ( i % 3 == 0 )
        {
            references.push_back({ functions[ i ].address,
                functions[ i / 3 ].address });
        }
    }

    const auto start = std::chrono::steady_clock::now();
    UniverseGraph_s graph = Universe_BuildGraph( functions, references );
    bool focus = false;
    bool organic = false;

    for ( int i = 2; i < argc; ++i )
    {
        focus |= std::strcmp( argv[ i ], "--focus" ) == 0;
        organic |= std::strcmp( argv[ i ], "--organic" ) == 0;
    }

    if ( organic )
    {
        Universe_RelayoutGraph( graph, true );
    }

    const UniverseGraph_s focused = focus
        ? Universe_FocusCallers( graph, graph.nodes[ 0 ].address )
        : UniverseGraph_s{};

    CubeCamera_s camera;
    camera.zoom = focus ? 1.12f : 1.0f;
    Image_s image;
    Universe_RenderInto( image, focus ? focused : graph, camera,
        focus ? 0u : CANTORDUST_NO_SOURCE, true,
        1000, 700, CDP_GHIDRA_VIOLET );

    const auto end = std::chrono::steady_clock::now();
    std::ofstream output( path, std::ios::binary );
    output << "P6\n" << image.width << " " << image.height << "\n255\n";

    for ( const std::uint32_t color : image.argb )
    {
        const char rgb[ 3 ] = {
            static_cast< char >( ( color >> 16 ) & 255u ),
            static_cast< char >( ( color >> 8 ) & 255u ),
            static_cast< char >( color & 255u )
        };
        output.write( rgb, 3 );
    }

    std::cout << "functions=" << graph.nodes.size()
              << " edges=" << graph.links.size()
              << " build+frame_ms=" <<
        std::chrono::duration_cast< std::chrono::milliseconds >(
            end - start ).count() << '\n';

    return output ? 0 : 1;
}
