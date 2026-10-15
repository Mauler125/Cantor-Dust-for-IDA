#include "pch/ida_pch.h"
#include "ida/cube_widget.h"
#include "cantordust/scalar_types.h"

#include "math/cube.h"

#include <QMouseEvent>
#include <QResizeEvent>

namespace cantordust
{

CubeWidget::CubeWidget( QWidget* const parent ) : QWidget( parent )
{
    setMinimumSize( 120, 120 );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    setMouseTracking( true );
    setCursor( Qt::CrossCursor );
}

void CubeWidget::SetCloud( CubeCloud_s cloud, const Palette_e palette )
{
    m_cloud = std::move( cloud );
    m_palette = palette;
    m_dirty = true;
    update();
}

void CubeWidget::SetRenderOptions( const CubeRenderOptions_s options )
{
    m_renderOptions = options;
    m_dirty = true;
    update();
}

void CubeWidget::SetOnNavigate(
    std::function< void( usize ) > callback )
{
    m_onNavigate = std::move( callback );
}

void CubeWidget::SetOnHover(
    std::function< void( usize ) > callback )
{
    m_onHover = std::move( callback );
}

void CubeWidget::EnsureImage()
{
    const int rasterWidth = std::clamp( width(), 1, 2048 );
    const int rasterHeight = std::clamp( height(), 1, 2048 );

    if ( !m_dirty && m_image.width == static_cast< usize >( rasterWidth ) &&
         m_image.height == static_cast< usize >( rasterHeight ) )
    {
        return;
    }

    Cube_RenderInto( m_image, m_depthBuffer, m_renderScratch,
        m_cloud, m_camera,
        static_cast< usize >( rasterWidth ),
        static_cast< usize >( rasterHeight ), m_palette, m_renderOptions );

    m_dirty = false;
}

usize CubeWidget::HitTest( const QPoint& position ) const
{
    if ( m_image.sourceIndex.empty() || width() <= 0 || height() <= 0 )
    {
        return CANTORDUST_NO_SOURCE;
    }

    const int x = static_cast< int >(
        static_cast< i64 >( position.x() ) * m_image.width / width() );

    const int y = static_cast< int >(
        static_cast< i64 >( position.y() ) * m_image.height / height() );

    const int radius = 6;
    int bestDistance = radius * radius + 1;
    usize best = CANTORDUST_NO_SOURCE;

    for ( int yy = -radius; yy <= radius; ++yy )
    {
        for ( int xx = -radius; xx <= radius; ++xx )
        {
            const int distance = xx * xx + yy * yy;

            if ( distance >= bestDistance )
            {
                continue;
            }

            const int sx = x + xx;
            const int sy = y + yy;

            if ( sx < 0 || sy < 0 )
            {
                continue;
            }

            const usize source = m_image.SourceAt(
                static_cast< usize >( sx ),
                static_cast< usize >( sy ) );

            if ( source != CANTORDUST_NO_SOURCE )
            {
                best = source;
                bestDistance = distance;
            }
        }
    }

    return best;
}

void CubeWidget::paintEvent( QPaintEvent* )
{
    EnsureImage();
    QPainter painter( this );
    painter.fillRect( rect(), QColor::fromRgba(
        Palette_Get( m_palette ).background3d ) );

    if ( m_image.argb.empty() )
    {
        return;
    }

    const QImage bitmap(
        reinterpret_cast< const uchar* >( m_image.argb.data() ),
        static_cast< int >( m_image.width ),
        static_cast< int >( m_image.height ),
        static_cast< int >( m_image.width * sizeof( u32 ) ),
        QImage::Format_ARGB32 );

    painter.setRenderHint( QPainter::SmoothPixmapTransform, false );
    painter.drawImage( rect(), bitmap );

    // Small always-visible orientation triad, independent of point geometry.
    // Red X, green Y, blue Z; use the exact same camera transform as the cloud.
    painter.setRenderHint( QPainter::Antialiasing, true );

    const CubeTransform_s t = Math_CubeTransform( m_camera );
    const QPointF origin( width() - 53.0, 58.0 );
    const CubeProjection_s zero = Math_CubeProject( 0, 0, 0, t );
    struct Axis_s { f32 x, y, z; QColor color; const char* name; };

    const Axis_s axes[] = {
        { 1, 0, 0, QColor( 243, 82, 82 ), "X" },
        { 0, 1, 0, QColor( 74, 220, 111 ), "Y" },
        { 0, 0, 1, QColor( 82, 151, 255 ), "Z" }
    };
    QColor triadBackground = QColor::fromRgba(
        Palette_Get( m_palette ).background3d );
    triadBackground.setAlpha( 170 );
    painter.setBrush( triadBackground );
    painter.setPen( Qt::NoPen );
    painter.drawEllipse( origin, 35.0, 35.0 );
    QFont labelFont = painter.font();
    labelFont.setBold( true );
    labelFont.setPointSize( 8 );
    painter.setFont( labelFont );

    for ( const Axis_s& axis : axes )
    {
        const CubeProjection_s p = Math_CubeProject( axis.x, axis.y, axis.z, t );
        QPointF v( ( p.x - zero.x ) * 33.0f,
                   -( p.y - zero.y ) * 33.0f );

        const qreal len = std::hypot( v.x(), v.y() );

        if ( len > 27.0 )
            v *= 27.0 / len;

        const QPointF end = origin + v;
        painter.setPen( QPen( axis.color, 2.0, Qt::SolidLine, Qt::RoundCap ) );
        painter.drawLine( origin, end );
        painter.setBrush( axis.color );
        painter.drawEllipse( end, 3.0, 3.0 );
        painter.drawText( QRectF( end.x() - 8.0, end.y() - 17.0, 16.0, 14.0 ),
            Qt::AlignCenter, QString::fromLatin1( axis.name ) );
    }

}

void CubeWidget::resizeEvent( QResizeEvent* const event )
{
    m_dirty = true;
    QWidget::resizeEvent( event );
}

void CubeWidget::mousePressEvent( QMouseEvent* const event )
{
    if ( event->button() != Qt::LeftButton )
    {
        QWidget::mousePressEvent( event );
        return;
    }

    m_dragging = true;
    m_rotated = false;
    m_lastPosition = event->pos();
    m_pressPosition = event->pos();
    setCursor( Qt::ClosedHandCursor );
    event->accept();
}

void CubeWidget::mouseMoveEvent( QMouseEvent* const event )
{
    if ( m_dragging && event->buttons().testFlag( Qt::LeftButton ) )
    {
        const QPoint delta = event->pos() - m_lastPosition;
        m_lastPosition = event->pos();

        if ( !delta.isNull() )
        {
            Cube_Rotate( m_camera,
                static_cast< f32 >( delta.x() ),
                static_cast< f32 >( delta.y() ) );

            if ( ( event->pos() - m_pressPosition ).manhattanLength() >= 4 )
            {
                m_rotated = true;
            }

            m_dirty = true;
            update();
        }

        event->accept();
        return;
    }

    if ( m_onHover )
    {
        EnsureImage();
        m_onHover( HitTest( event->pos() ) );
    }

    event->accept();
}

void CubeWidget::mouseReleaseEvent( QMouseEvent* const event )
{
    if ( event->button() != Qt::LeftButton || !m_dragging )
    {
        QWidget::mouseReleaseEvent( event );
        return;
    }

    m_dragging = false;
    setCursor( Qt::CrossCursor );

    if ( !m_rotated && m_onNavigate )
    {
        EnsureImage();

        const usize source = HitTest( event->pos() );

        if ( source != CANTORDUST_NO_SOURCE )
        {
            m_onNavigate( source );
        }
    }

    event->accept();
}

void CubeWidget::mouseDoubleClickEvent( QMouseEvent* const event )
{
    if ( event->button() == Qt::LeftButton )
    {
        m_camera = {};
        m_dirty = true;
        update();
        event->accept();
        return;
    }

    QWidget::mouseDoubleClickEvent( event );
}

void CubeWidget::leaveEvent( QEvent* const event )
{
    if ( m_onHover )
    {
        m_onHover( CANTORDUST_NO_SOURCE );
    }

    QWidget::leaveEvent( event );
}

void CubeWidget::wheelEvent( QWheelEvent* const event )
{
    m_wheelRemainder += event->angleDelta().y();

    const int steps = m_wheelRemainder / 120;
    m_wheelRemainder -= steps * 120;

    if ( steps != 0 )
    {
        Cube_Zoom( m_camera, steps );
        m_dirty = true;
        update();
    }

    event->accept();
}

} // namespace cantordust
