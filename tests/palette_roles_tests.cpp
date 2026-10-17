#include "cantordust/cube.h"
#include "cantordust/palette.h"
#include "cantordust/renderer.h"
#include "cantordust/universe.h"

#include <cassert>
#include <string>

using namespace cantordust;

int main()
{
    const Palette_s& reference = Palette_Get( CDP_REFERENCE );
    const Palette_s& ghidra = Palette_Get( CDP_GHIDRA_VIOLET );

    assert( Palette_Count() == CDP_COUNT );
    assert( reference.background3d == 0xFF000000u );
    assert( reference.background == 0xFF151C28u ); // 2D unchanged
    assert( reference.selection == 0xFF39FF70u ); // original RGB selection green
    assert( ghidra.selection == 0xFF39FF70u ); // selected node is neon green
    assert( ghidra.focusCallee == 0xFFEFFF24u ); // callee stays yellow
    assert( ghidra.selection != ghidra.focusCallee );
    assert( reference.focusCallee == 0xFFFFFF00u ); // focused callee stays yellow
    assert( reference.selection != reference.focusCallee );
    assert( Universe_SelectionColor( CDP_REFERENCE ) == reference.selection );
    assert( Universe_SelectionColor( CDP_GHIDRA_VIOLET ) == ghidra.selection );
    assert( ghidra.heatMidpoint == 0xFF00BCFFu );

    for ( usize index = 0; index < Palette_Count(); ++index )
    {
        const Palette_e mode = static_cast< Palette_e >( index );
        const Palette_s& palette = Palette_Get( mode );

        assert( palette.selection >> 24 == 0xFFu );
        assert( palette.background3d >> 24 == 0xFFu );
        assert( palette.selection != palette.background3d );
        assert( Universe_SelectionColor( mode ) == palette.selection );

        // The Cube still draws its orientation wireframe without data, so
        // check unobstructed corners instead of requiring a completely blank
        // image. The empty universe draws only its background.
        const Image_s cloud = Cube_Render( {}, {}, 320u, 240u, mode );
        assert( cloud.argb.size() == 320u * 240u );
        assert( cloud.argb.front() == palette.background3d );
        assert( cloud.argb.back() == palette.background3d );

        Image_s universe;
        Universe_RenderInto( universe, {}, {}, CANTORDUST_NO_SOURCE,
            false, 320u, 240u, mode );
        assert( universe.argb.size() == 320u * 240u );

        for ( const u32 pixel : universe.argb )
            assert( pixel == palette.background3d );
    }

    assert( Palette_Get( CDP_COUNT ).selection == reference.selection );

    // SVG vector export must use the same 3D background as the on-screen view.
    const std::string svg = Universe_ExportSvg( {}, {}, {}, {},
        CDP_REFERENCE, false, CANTORDUST_NO_SOURCE, 320u, 240u );
    assert( svg.find( "fill=\"#000000\"" ) != std::string::npos );

    return 0;
}
