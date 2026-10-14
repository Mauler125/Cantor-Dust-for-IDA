#ifndef CANTORDUST_IDA_RANGE_SELECTION_STYLE_H
#define CANTORDUST_IDA_RANGE_SELECTION_STYLE_H

#include "cantordust/palette.h"

#include <QColor>

namespace cantordust
{

// Keep the source samples visible beneath the selection while tinting its
// translucent fill and contour with the active palette's 3D selection hue.
inline QColor RangeSelection_Fill( const Palette_e palette )
{
    QColor color = QColor::fromRgba( Palette_Get( palette ).selection );
    color.setAlpha( 38 );
    return color;
}

inline QColor RangeSelection_Border( const Palette_e palette )
{
    QColor color = QColor::fromRgba( Palette_Get( palette ).selection );
    color.setAlpha( 185 );
    return color;
}

} // namespace cantordust

#endif // CANTORDUST_IDA_RANGE_SELECTION_STYLE_H
