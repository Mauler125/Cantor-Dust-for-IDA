#ifndef IDA_ENTROPY_WIDGET_H
#define IDA_ENTROPY_WIDGET_H


#include "cantordust/scalar_types.h"
#include "math/entropy.h"
#include "cantordust/palette.h"

#include <QtWidgets>

#include <vector>
#include <functional>

namespace cantordust
{

class EntropyWidget final : public QWidget
{
public:
    explicit EntropyWidget( QWidget* parent = nullptr );

    void SetSamples( std::vector< EntropySample_s > samples );
    void SetPalette( Palette_e palette );
    void SetSelection( f64 start, f64 end );
    void SetOnDrag( std::function< void( f64 ) > callback );

protected:
    void paintEvent( QPaintEvent* event ) override;
    void mousePressEvent( QMouseEvent* event ) override;
    void mouseMoveEvent( QMouseEvent* event ) override;
    void mouseReleaseEvent( QMouseEvent* event ) override;

private:
    std::vector< EntropySample_s > m_samples;
    Palette_e m_palette = CDP_REFERENCE;
    f64 m_selectionStart = 0.0;
    f64 m_selectionEnd = 0.0;
    f64 m_dragAnchor = 0.0;
    bool m_dragging = false;
    std::function< void( f64 ) > m_onDrag;
    void DragAt( const QPoint& point );
};

} // namespace cantordust

#endif // IDA_ENTROPY_WIDGET_H
