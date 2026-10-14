#ifndef IDA_CUBE_WIDGET_H
#define IDA_CUBE_WIDGET_H


#include "cantordust/scalar_types.h"
#include "cantordust/cube.h"

#include <QtWidgets>

#include <cstddef>
#include <functional>

namespace cantordust
{

class CubeWidget final : public QWidget
{
public:
    explicit CubeWidget( QWidget* parent = nullptr );

    void SetCloud( CubeCloud_s cloud, Palette_e palette );
    void SetRenderOptions( CubeRenderOptions_s options );
    void SetOnNavigate( std::function< void( usize ) > callback );
    void SetOnHover( std::function< void( usize ) > callback );

protected:
    void paintEvent( QPaintEvent* event ) override;
    void resizeEvent( QResizeEvent* event ) override;
    void mousePressEvent( QMouseEvent* event ) override;
    void mouseMoveEvent( QMouseEvent* event ) override;
    void mouseReleaseEvent( QMouseEvent* event ) override;
    void mouseDoubleClickEvent( QMouseEvent* event ) override;
    void leaveEvent( QEvent* event ) override;
    void wheelEvent( QWheelEvent* event ) override;

private:
    void EnsureImage();
    usize HitTest( const QPoint& position ) const;

    CubeCloud_s m_cloud;
    CubeCamera_s m_camera;
    CubeRenderOptions_s m_renderOptions;
    Image_s m_image;
    std::vector< f32 > m_depthBuffer;
    CubeRenderScratch_s m_renderScratch;
    Palette_e m_palette = CDP_REFERENCE;
    std::function< void( usize ) > m_onNavigate;
    std::function< void( usize ) > m_onHover;
    QPoint m_lastPosition;
    QPoint m_pressPosition;
    int m_wheelRemainder = 0;
    bool m_dragging = false;
    bool m_rotated = false;
    bool m_dirty = true;
};

} // namespace cantordust

#endif // IDA_CUBE_WIDGET_H
