#include "cantordust/universe.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

using namespace cantordust;

static UniverseData_s Example()
{
    UniverseData_s data;
    data.functions.push_back({ 0x10000, "network_dispatch" });

    for ( std::uint32_t i = 1; i <= 46; ++i )
    {
        const std::uint64_t address = 0x10000u + 0x100u * i;
        data.functions.push_back({ address,
            i % 4 == 0 ? "on_packet_" + std::to_string( i ) :
                "worker_" + std::to_string( i ) });

        const bool caller = i <= 24;
        const std::uint64_t from = caller ? address : 0x10000u;
        const std::uint64_t to = caller ? 0x10000u : address;

        for ( std::uint32_t k = 0; k < 1u + i % 9u; ++k )
            data.references.push_back({ from, to,
                from + 0x10u + k * 2u });

        if ( i == 5 || i == 13 || i == 32 )
            data.references.push_back({ to, from, to + 0x72u });
    }

    for ( std::uint32_t i = 0; i < 8; ++i )
    {
        const std::uint64_t address = 0x20000u + 0x100u * i;
        data.functions.push_back({ address, "pipeline_stage_" +
            std::to_string( i ) });

        if ( i > 0 )
            data.references.push_back({ address - 0x100u, address,
                address - 0xE0u });
    }

    return data;
}

int main( int argc, char** argv )
{
    if ( argc < 3 )
    {
        std::cerr << "usage: preview --both|--path output.ppm\n";
        return 1;
    }

    const UniverseData_s data = Example();
    UniverseGraph_s graph;

    if ( std::string( argv[ 1 ] ) == "--both" )
        graph = Universe_FocusConnectionsFromData(
            data, 0x10000u, UFM_BOTH );

    else
    {
        const UniverseCallPath_s path = Universe_FindCallPath(
            data, 0x20000u, 0x20700u );

        graph = Universe_BuildPathGraph( data, path );
    }

    CubeCamera_s camera;
    camera.zoom = graph.pathMode ? 1.0f : 0.82f;
    Image_s image;
    UniverseRenderStyle_s style;
    style.antiAliasing = true;
    Universe_RenderInto( image, graph, camera,
        CANTORDUST_NO_SOURCE, true, 1100, 650, CDP_GHIDRA_VIOLET, {}, style );

    std::ofstream output( argv[ 2 ], std::ios::binary );
    output << "P6\n" << image.width << " " << image.height << "\n255\n";

    for ( const std::uint32_t c : image.argb )
    {
        const char rgb[ 3 ] = { static_cast< char >( c >> 16 ),
            static_cast< char >( c >> 8 ), static_cast< char >( c ) };

        output.write( rgb, 3 );
    }

    return output ? 0 : 1;
}
