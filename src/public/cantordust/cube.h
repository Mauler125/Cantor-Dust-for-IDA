#ifndef CANTORDUST_CUBE_H
#define CANTORDUST_CUBE_H


#include "cantordust/scalar_types.h"
#include "cantordust/types.h"
#include "cantordust/palette.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#define CANTORDUST_CUBE_GRID 128u
#define CANTORDUST_CUBE_MAX_POINTS 131072u

namespace cantordust
{

// Spatial mappings keep the source-byte identity stable for navigation.
enum CubeGeometry_e : int { CCG_CUBE = 0, CCG_SPHERE, CCG_CYLINDER, CCG_TETRAHEDRON };
enum CubeValueMode_e : int {
    CCV_U8 = 0, CCV_U16_LE, CCV_U16_BE, CCV_U32_LE, CCV_U32_BE,
    CCV_DELTA8, CCV_POPCOUNT8
};

struct CubeRenderOptions_s
{
    CubeGeometry_e geometry = CCG_CUBE;
    f32 pointSize = 1.0f;
    f32 brightness = 1.0f;
};

// The first byte is the navigable anchor. u8 uses the following three bytes;
// u16/u32 use three aligned scalar samples starting at that anchor.
// The decoder uses the most significant byte of each integer.
struct CubeTriplet_s;
usize Cube_SampleLength( CubeValueMode_e mode );
bool Cube_DecodeTriplet( const u8* bytes,
    CubeValueMode_e mode, CubeTriplet_s& result );

struct CubePoint_s
{
    f32 x = 0;
    f32 y = 0;
    f32 z = 0;
    u32 count = 0;
    usize sourceIndex = CANTORDUST_NO_SOURCE;
    u32 argb = 0;
    u8 sourceByte = 0;
};

// Position comes from X/Y/Z; colorByte is the source byte at sourceIndex.
struct CubeTriplet_s
{
    u8 x = 0;
    u8 y = 0;
    u8 z = 0;
    u8 colorByte = 0;
    usize sourceIndex = CANTORDUST_NO_SOURCE;
};

struct CubeCloud_s
{
    std::vector< CubePoint_s > points;
    usize acceptedTriplets = 0;
    usize occupiedVoxels = 0;
};

struct CubeRenderScratch_s
{
    std::vector< f32 > red;
    std::vector< f32 > green;
    std::vector< f32 > blue;
};

struct CubeCamera_s
{
    f32 yaw = 0.68f;
    f32 pitch = 0.38f;
    f32 zoom = 1.0f;
};

CubeCloud_s Cube_BuildCloud(
    const std::vector< ByteCell_s >& cells,
    Palette_e palette,
    usize maximumPoints = CANTORDUST_CUBE_MAX_POINTS,
    const std::vector< usize >& boundaryOffsets = {},
    CubeValueMode_e valueMode = CCV_U8 );

CubeCloud_s Cube_BuildTriplets(
    const std::vector< CubeTriplet_s >& triplets,
    Palette_e palette,
    usize maximumPoints = CANTORDUST_CUBE_MAX_POINTS );

void Cube_RenderInto(
    Image_s& image, std::vector< f32 >& depthBuffer,
    const CubeCloud_s& cloud,
    const CubeCamera_s& camera,
    usize width,
    usize height,
    Palette_e palette, CubeRenderOptions_s options = {} );

void Cube_RenderInto(
    Image_s& image, std::vector< f32 >& depthBuffer,
    CubeRenderScratch_s& scratch,
    const CubeCloud_s& cloud,
    const CubeCamera_s& camera,
    usize width,
    usize height,
    Palette_e palette, CubeRenderOptions_s options = {} );

Image_s Cube_Render(
    const CubeCloud_s& cloud,
    const CubeCamera_s& camera,
    usize width,
    usize height,
    Palette_e palette, CubeRenderOptions_s options = {} );

void Cube_Rotate( CubeCamera_s& camera, f32 horizontal, f32 vertical );
void Cube_Zoom( CubeCamera_s& camera, int wheelSteps,
    f32 minZoom = 0.35f, f32 maxZoom = 4.0f );

} // namespace cantordust

#endif // CANTORDUST_CUBE_H
