#ifndef CANTORDUST_CONSTANTS_H
#define CANTORDUST_CONSTANTS_H


#include "cantordust/scalar_types.h"
namespace cantordust
{

// C++17 has no std::numbers; keep typed, shared values here instead of
// repeating floating-point approximations or using preprocessor macros.
inline constexpr f32 CANTORDUST_PI_F = 3.14159265358979323846f;
inline constexpr f32 CANTORDUST_TAU_F = 6.28318530717958647692f;
inline constexpr f32 CANTORDUST_HALF_PI_F = 1.57079632679489661923f;
inline constexpr f64 CANTORDUST_TAU_D = 6.283185307179586;

// Normalizing 8-bit color channels and animation levels to [0, 1].
inline constexpr f32 CANTORDUST_BYTE_MAX_F = 255.0f;

// Keep every CPU, SVG, and line-rendering perspective calculation aligned.
inline constexpr f32 CANTORDUST_CAMERA_PERSPECTIVE_ZOOM = 1.55f;
inline constexpr f32 CANTORDUST_CAMERA_PERSPECTIVE_DISTANCE = 3.0f;
inline constexpr f32 CANTORDUST_CAMERA_PERSPECTIVE_DEPTH_WEIGHT = 0.33f;

} // namespace cantordust

#endif // CANTORDUST_CONSTANTS_H
