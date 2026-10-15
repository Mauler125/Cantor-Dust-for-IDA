#include "cantordust/palette.h"
#include "cantordust/scalar_types.h"

namespace cantordust
{

static const Palette_s s_palettes[ CDP_COUNT ] = {
    // Byte colors, 2D background, histogram, density low/high; then
    // 3D background, selection, focused callee, optional heat midpoint.
    { "Reference RGB",     0xFF000000u, 0xFF0000F0u, 0xFF00F000u,
      0xFFF00000u, 0xFFFFFFFFu, 0xFF303844u, 0xFF151C28u,
      0xFF67D9C3u, 0xFF143741u, 0xFF60F5E6u,
      0xFF000000u, 0xFF39FF70u, 0xFFFFFF00u, 0x00000000u },
    { "Ghidra Violet",     0xFF090019u, 0xFF3827EFu, 0xFF9935FFu,
      0xFFFF27CDu, 0xFFFFECFFu, 0xFF302641u, 0xFF10071Eu,
      0xFFD66DFFu, 0xFF270A54u, 0xFFF856FFu,
      0xFF10071Eu, 0xFF39FF70u, 0xFFEFFF24u, 0xFF00BCFFu },
    { "Aurora",            0xFF08121Bu, 0xFF1986C2u, 0xFF26E6AFu,
      0xFFA46CFFu, 0xFFE9FFF6u, 0xFF263A48u, 0xFF0A1B28u,
      0xFF52F3D1u, 0xFF135271u, 0xFFB0FFD2u,
      0xFF0A1B28u, 0xFFFFB479u, 0xFFFFD76Au, 0x00000000u },
    { "Ember",             0xFF160905u, 0xFF813B20u, 0xFFFFB21Fu,
      0xFFFD4930u, 0xFFFFF1AEu, 0xFF493024u, 0xFF200E0Au,
      0xFFFF9130u, 0xFF783024u, 0xFFFFDD72u,
      0xFF200E0Au, 0xFF85FFA0u, 0xFF58EFFFu, 0x00000000u },
    { "Glacier",           0xFF061420u, 0xFF1B487Du, 0xFF7BD6FFu,
      0xFF3A8EDCu, 0xFFFFFFFFu, 0xFF2A4054u, 0xFF091D2Eu,
      0xFFA9EEFFu, 0xFF194E85u, 0xFFE3FBFFu,
      0xFF091D2Eu, 0xFFFF7AD5u, 0xFFFFC36Au, 0x00000000u },
    { "Matrix",            0xFF000900u, 0xFF155D12u, 0xFF00FF62u,
      0xFF9AE12Bu, 0xFFE5FFD3u, 0xFF1E3322u, 0xFF001005u,
      0xFF66FE65u, 0xFF176329u, 0xFFCEFF78u,
      0xFF001005u, 0xFFFF61CFu, 0xFFFF57D8u, 0x00000000u },
    { "Synthwave",         0xFF150728u, 0xFF4267FAu, 0xFFFF4FD8u,
      0xFFFF974Eu, 0xFFFBF3FFu, 0xFF39264Fu, 0xFF170E31u,
      0xFFFF55C8u, 0xFF4C2391u, 0xFFFFACD8u,
      0xFF170E31u, 0xFFEFFF66u, 0xFF65FFD1u, 0x00000000u },
    { "Solarized",         0xFF002B36u, 0xFF268BD2u, 0xFF859900u,
      0xFFCB4B16u, 0xFFFDF6E3u, 0xFF586E75u, 0xFF073642u,
      0xFFB58900u, 0xFF2D6C77u, 0xFFEEE8B9u,
      0xFF073642u, 0xFFFF76B9u, 0xFFFFD052u, 0x00000000u },
    { "Oceanic",           0xFF001824u, 0xFF2055A1u, 0xFF1BCBCEu,
      0xFF0E8F8Du, 0xFFD4FFFFu, 0xFF2B485Au, 0xFF082637u,
      0xFF24D7D3u, 0xFF12516Cu, 0xFF66F0CEu,
      0xFF082637u, 0xFFFFC36Cu, 0xFFFF9274u, 0x00000000u },
    { "Infrared",          0xFF100003u, 0xFF6E0529u, 0xFFFF193Fu,
      0xFFFF781Bu, 0xFFFFFBD0u, 0xFF4D1E2Au, 0xFF20020Cu,
      0xFFFF5D28u, 0xFF7F1127u, 0xFFFFE34Bu,
      0xFF20020Cu, 0xFFB3FF68u, 0xFF54F2FFu, 0x00000000u },
    { "Monochrome",        0xFF050505u, 0xFF494949u, 0xFFBABABAu,
      0xFF777777u, 0xFFFFFFFFu, 0xFF303030u, 0xFF121212u,
      0xFFE8E8E8u, 0xFF333333u, 0xFFFFFFFFu,
      0xFF121212u, 0xFFFFFFFFu, 0xFFEDEDEDu, 0x00000000u },
    { "Gold & Onyx",       0xFF080907u, 0xFF575126u, 0xFFFFD65Bu,
      0xFFD98B21u, 0xFFFFFFDBu, 0xFF37352Au, 0xFF15150Du,
      0xFFFFC94Au, 0xFF54441Bu, 0xFFFFFF9Bu,
      0xFF15150Du, 0xFFFFE16Au, 0xFF57DFFFu, 0x00000000u },
    { "Pastel",            0xFF353146u, 0xFFB8CBFFu, 0xFFA9F0C3u,
      0xFFFFA8BFu, 0xFFFFFFD9u, 0xFF6A6178u, 0xFF302F41u,
      0xFFD1A2FFu, 0xFF796EAAu, 0xFFFFD9E8u,
      0xFF302F41u, 0xFFD392FFu, 0xFFFFEBA8u, 0x00000000u },
    { "Toxic",             0xFF050A00u, 0xFF5D2FC4u, 0xFFCEFF00u,
      0xFFCA24FFu, 0xFFF8FFDFu, 0xFF3B3343u, 0xFF12170Bu,
      0xFFC7FD13u, 0xFF493AA0u, 0xFFEAFF35u,
      0xFF12170Bu, 0xFFFF9D40u, 0xFF4BF3FFu, 0x00000000u },
    { "Blueprint",         0xFF04102Bu, 0xFF163C86u, 0xFF61AEFFu,
      0xFF3169D8u, 0xFFFFFFFFu, 0xFF253A64u, 0xFF091943u,
      0xFF83C6FFu, 0xFF1E4DA9u, 0xFFD0E9FFu,
      0xFF091943u, 0xFF65FFD1u, 0xFFFFC857u, 0x00000000u },
    { "Copper Patina",     0xFF101513u, 0xFF386F70u, 0xFF68D8BFu,
      0xFFDD773Bu, 0xFFFFECD0u, 0xFF354740u, 0xFF142C29u,
      0xFFDB9864u, 0xFF3C7773u, 0xFFF5C68Au,
      0xFF142C29u, 0xFFFFE6A5u, 0xFFE4A2FFu, 0x00000000u }
};

u32 Palette_ByteColor(
    const u8 byte, const Palette_s& palette )
{
    if ( byte == 0 )
    {
        return palette.zero;
    }

    if ( byte < 0x20 )
    {
        return palette.low;
    }

    if ( byte <= 0x7F )
    {
        return palette.printable;
    }

    if ( byte < 0xFF )
    {
        return palette.high;
    }

    return palette.full;
}

const Palette_s& Palette_Get( const Palette_e index )
{
    const unsigned int ordinal = static_cast< unsigned int >( index );
    return ordinal < CDP_COUNT ? s_palettes[ ordinal ]
                               : s_palettes[ CDP_REFERENCE ];
}

usize Palette_Count()
{
    return CDP_COUNT;
}

} // namespace cantordust
