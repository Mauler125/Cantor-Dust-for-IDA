#include "ida/entropy_widget.h"
#include "cantordust/scalar_types.h"
#include "ida/range_selection_style.h"

#include "math/color.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace cantordust
{

static QColor Entropy_Color(
    const EntropySample_s& sample, const Palette_e paletteIndex )
{
    if ( !sample.valid )
    {
        return QColor::fromRgba( Palette_Get( paletteIndex ).missing );
    }

    if ( paletteIndex != CDP_REFERENCE )
    {
        const Palette_s& palette = Palette_Get( paletteIndex );
        return QColor::fromRgba( Math_BlendColor(
            palette.densityLow, palette.densityHigh,
            sample.bits / 8.0 ) );
    }

    const f64 normalized = std::clamp( sample.bits / 8.0, 0.0, 1.0 );
    const int red = static_cast< int >( 15.0 + 240.0 * normalized );
    const int green = static_cast< int >(
        normalized < 0.5 ? 70.0 + 300.0 * normalized
                         : 220.0 - 380.0 * ( normalized - 0.5 ) );

    const int blue = static_cast< int >( 180.0 - 140.0 * normalized );
    return QColor( red, std::clamp( green, 0, 255 ), blue );
}

EntropyWidget::EntropyWidget( QWidget* const parent ) : QWidget( parent )
{
    setFixedWidth( 38 );
    setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Expanding );
    setToolTip( "Sampled byte entropy (0-8 bits/byte). "
                "Drag the highlighted window to navigate." );

    setCursor( Qt::OpenHandCursor );
}

void EntropyWidget::SetPalette( const Palette_e palette )
{
    m_palette = palette;
    update();
}

void EntropyWidget::SetSamples( std::vector< EntropySample_s > samples )
{
    m_samples = std::move( samples );
    update();
}

void EntropyWidget::SetSelection( const f64 start, const f64 end )
{
    m_selectionStart = std::clamp( start, 0.0, 1.0 );
    m_selectionEnd = std::clamp( end, 0.0, 1.0 );
    update();
}

void EntropyWidget::SetOnDrag( std::function< void( f64 ) > callback )
{
    m_onDrag = std::move( callback );
}

void EntropyWidget::DragAt( const QPoint& point )
{
    if ( height() <= 0 || !m_onDrag )
        return;

    const f64 position = std::clamp(
        static_cast< f64 >( point.y() ) / height(), 0.0, 1.0 );

    const f64 length = std::clamp(
        m_selectionEnd - m_selectionStart, 0.0, 1.0 );

    m_onDrag( std::clamp( position - m_dragAnchor, 0.0, 1.0 - length ) );
}

void EntropyWidget::mousePressEvent( QMouseEvent* const event )
{
    if ( event->button() != Qt::LeftButton )
    {
        QWidget::mousePressEvent( event );
        return;
    }

    const f64 position = height() > 0
        ? static_cast< f64 >( event->pos().y() ) / height() : 0.0;

    m_dragAnchor = position >= m_selectionStart &&
        position <= m_selectionEnd
        ? position - m_selectionStart
        : ( m_selectionEnd - m_selectionStart ) * 0.5;

    m_dragging = true;
    setCursor( Qt::ClosedHandCursor );
    DragAt( event->pos() );
    event->accept();
}

void EntropyWidget::mouseMoveEvent( QMouseEvent* const event )
{
    if ( m_dragging && ( event->buttons() & Qt::LeftButton ) )
    {
        DragAt( event->pos() );
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent( event );
}

void EntropyWidget::mouseReleaseEvent( QMouseEvent* const event )
{
    if ( event->button() == Qt::LeftButton && m_dragging )
    {
        DragAt( event->pos() );
        m_dragging = false;
        setCursor( Qt::OpenHandCursor );
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent( event );
}

void EntropyWidget::paintEvent( QPaintEvent* )
{
    QPainter painter( this );
    painter.fillRect( rect(),
        QColor::fromRgba( Palette_Get( m_palette ).background ) );

    if ( m_samples.empty() || height() <= 0 )
    {
        return;
    }

    const int stripHeight = height();
    const int count = static_cast< int >( m_samples.size() );

    for ( int y = 0; y < stripHeight; ++y )
    {
        const int index = static_cast< int >(
            static_cast< i64 >( y ) * count / stripHeight );

        painter.fillRect( 0, y, width(), 1, Entropy_Color( m_samples[ index ], m_palette ) );
    }

    const int first = std::clamp(
        static_cast< int >( m_selectionStart * stripHeight ), 0, stripHeight - 1 );

    const int last = std::clamp(
        static_cast< int >( m_selectionEnd * stripHeight ), first, stripHeight );

    const QRect selection( 0, first, width(), std::max( 1, last - first ) );
    painter.fillRect( selection, RangeSelection_Fill( m_palette ) );
    painter.setPen( QPen( RangeSelection_Border( m_palette ), 1 ) );
    painter.setBrush( Qt::NoBrush );
    painter.drawRect( selection.adjusted( 0, 0, -1, -1 ) );
}

} // namespace cantordust
