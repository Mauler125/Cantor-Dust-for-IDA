#ifndef CANTORDUST_RENDERER_H
#define CANTORDUST_RENDERER_H


#include "cantordust/scalar_types.h"
#include "cantordust/types.h"
#include "cantordust/palette.h"

namespace cantordust
{

enum class HistogramScale_e { Logarithmic, Linear, SquareRoot };
enum class DigraphIntensity_e { Frequency, ConditionalProbability };

struct BytePlotOptions_s
{
    HistogramScale_e histogramScale = HistogramScale_e::Logarithmic;
    DigraphIntensity_e digraphIntensity = DigraphIntensity_e::Frequency;
    usize digraphDistance = 1;
};

Image_s Render( View_e view, const std::vector< ByteCell_s >& data,
    usize side = CANTORDUST_SIDE,
    Palette_e palette = CDP_REFERENCE,
    BytePlotOptions_s options = {} );

Image_s RenderDetail( View_e view,
    const std::vector< ByteCell_s >& data,
    usize side = CANTORDUST_SIDE,
    Palette_e palette = CDP_REFERENCE,
    BytePlotOptions_s options = {} );

Image_s RenderHilbertOverview(
    const std::vector< ByteClassSummary_s >& classes,
    const std::vector< usize >& sourceOffsets,
    usize side = CANTORDUST_SIDE,
    Palette_e palette = CDP_REFERENCE );

Image_s RenderOverview(
    View_e view,
    const std::vector< ByteClassSummary_s >& classes,
    const std::vector< ByteCell_s >& data,
    const std::vector< usize >& sourceOffsets,
    usize side,
    Palette_e palette = CDP_REFERENCE,
    BytePlotOptions_s options = {} );

Image_s RenderLinearMapped(
    const std::vector< ByteCell_s >& data,
    const std::vector< usize >& sourceOffsets,
    usize side = CANTORDUST_SIDE,
    Palette_e palette = CDP_REFERENCE );

} // namespace cantordust

#endif // CANTORDUST_RENDERER_H
