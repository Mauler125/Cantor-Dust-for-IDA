#ifndef CANTORDUST_PALETTE_H
#define CANTORDUST_PALETTE_H


#include "cantordust/scalar_types.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace cantordust
{

enum Palette_e
{
    CDP_REFERENCE = 0,
    CDP_GHIDRA_VIOLET,
    CDP_AURORA,
    CDP_EMBER,
    CDP_GLACIER,
    CDP_MATRIX,
    CDP_SYNTHWAVE,
    CDP_SOLARIZED,
    CDP_OCEANIC,
    CDP_INFRARED,
    CDP_MONOCHROME,
    CDP_GOLD_ONYX,
    CDP_PASTEL,
    CDP_TOXIC,
    CDP_BLUEPRINT,
    CDP_COPPER_PATINA,
    CDP_COUNT
};

struct Palette_s
{
    const char* name;
    u32 zero;
    u32 low;
    u32 printable;
    u32 high;
    u32 full;
    u32 missing;
    u32 background;
    u32 histogram;
    u32 densityLow;
    u32 densityHigh;
    u32 background3d;
    u32 selection;
    u32 focusCallee;
    // 0 means the ordinary low-to-high heat gradient; other values add a midpoint.
    u32 heatMidpoint;
};

u32 Palette_ByteColor( u8 byte, const Palette_s& palette );
// Immutable byte colors, computed once for each of the built-in palettes.
const std::array< u32, 256 >& Palette_ByteColorTable( Palette_e index );

const Palette_s& Palette_Get( Palette_e index );
usize Palette_Count();

} // namespace cantordust

#endif // CANTORDUST_PALETTE_H
