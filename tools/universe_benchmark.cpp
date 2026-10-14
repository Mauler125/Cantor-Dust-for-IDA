#include "cantordust/universe.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace cantordust;

int main()
{
    std::vector< UniverseFunction_s > funcs;
    std::vector< UniverseReference_s > refs;

    for ( std::uint32_t i = 0; i < 6000; ++i )
    {
        funcs.push_back({ 0x10000u + i * 0x100u, "fn" });
    }

    std::uint32_t seed = 0x1234567u;

    for ( std::uint32_t i = 0; i < 90000; ++i )
    {
        seed = seed * 1664525u + 1013904223u;

        const std::uint32_t caller = seed % 6000u;
        seed = seed * 1664525u + 1013904223u;

        const std::uint32_t callee = seed % 6000u;
        refs.push_back({ funcs[ caller ].address, funcs[ callee ].address });
    }

    const auto before = std::chrono::steady_clock::now();
    const UniverseGraph_s graph = Universe_BuildGraph( funcs, refs );
    const auto built = std::chrono::steady_clock::now();
    Image_s image;
    CubeCamera_s camera;

    for ( int i = 0; i < 5; ++i )
    {
        Cube_Rotate( camera, 50.0f, 15.0f );
        Universe_RenderInto( image, graph, camera, 0, true,
            1024, 768, CDP_GHIDRA_VIOLET );
    }

    const auto done = std::chrono::steady_clock::now();

    for ( int i = 0; i < 5; ++i )
    {
        Cube_Rotate( camera, 50.0f, 15.0f );
        Universe_RenderInto( image, graph, camera, 0, false,
            1024, 768, CDP_GHIDRA_VIOLET );
    }

    const auto focusDone = std::chrono::steady_clock::now();
    const auto focusTime = std::chrono::duration_cast<
        std::chrono::milliseconds >( focusDone - done ).count();

    const auto buildTime = std::chrono::duration_cast<
        std::chrono::milliseconds >( built - before ).count();

    const auto frameTime = std::chrono::duration_cast<
        std::chrono::milliseconds >( done - built ).count();

    std::cout << graph.nodes.size() << " nodes, " << graph.links.size()
              << " links; graph " << buildTime << " ms, 5 frames "
              << frameTime << " ms (" << frameTime / 5
              << " ms/frame), focused: " << focusTime / 5 << " ms/frame\n";
}
