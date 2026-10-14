#include "ida/universe_widget.h"
#include "cantordust/scalar_types.h"

#include <QMouseEvent>
#include <QPainter>
#include <QSaveFile>
#include <QImage>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QSizePolicy>
#include <QKeyEvent>
#include <QTimer>

#include "math/cube.h"
#include "cantordust/constants.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace cantordust
{

using std::sin;

namespace
{
constexpr int kPulseTimerIntervalMs = 33;
constexpr f32 kIdaSyncPulseRadiansPerTick = 0.42f;
constexpr f32 kRoutePulseRadiansPerTick = 0.24f;
}

UniverseWidget::UniverseWidget( QWidget* const parent ) : QWidget( parent )
{
    setMinimumSize( 120, 120 );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    setMouseTracking( true );
    setCursor( Qt::CrossCursor );
    setFocusPolicy( Qt::StrongFocus );
    // One timer drives both small visual accents, avoiding redundant
    // full-raster frames when Follow IDA and a route are both active.
    m_highlightPulseTimer = new QTimer( this );
    m_highlightPulseTimer->setInterval( kPulseTimerIntervalMs );
    QObject::connect( m_highlightPulseTimer, &QTimer::timeout,
        this, [this]() { UpdatePulses(); } );
}

void UniverseWidget::SetGraph( const UniverseGraph_s* const graph,
    const Palette_e palette )
{
    m_graph = graph;

    if ( m_autoFraming && graph != nullptr )
    {
        m_view = Universe_DefaultView( *graph );
    }

    m_selectedIndex = CANTORDUST_NO_SOURCE;

    if ( graph != nullptr )
    {
        if ( m_hasSelection )
        {
            for ( usize i = 0; i < graph->nodes.size(); ++i )
            {
                if ( graph->nodes[ i ].address == m_selectedAddress )
                {
                    m_selectedIndex = i;
                    break;
                }
            }
        }
    }

    m_idaSyncIndex = CANTORDUST_NO_SOURCE;

    if ( graph != nullptr && m_idaSyncAddress.has_value() )
    {
        for ( usize i = 0; i < graph->nodes.size(); ++i )
        {
            if ( graph->nodes[ i ].address == *m_idaSyncAddress )
            {
                m_idaSyncIndex = i;
                break;
            }
        }
    }
    // The active route can change while the same widget remains visible.
    const bool animatePath = graph != nullptr && graph->multiPathMode &&
        !graph->links.empty();

    if ( !animatePath || !m_pathPulseActive ||
         graph->activePathRoute != m_pulsingRoute )
    {
        m_pathPulsePhase = -CANTORDUST_HALF_PI_F;
        m_pathPulseLevel = 0u;
    }

    m_pathPulseActive = animatePath;
    m_pulsingRoute = animatePath ? graph->activePathRoute : 0u;
    RefreshPulseTimer();
    m_palette = palette;
    m_dirty = true;
    update();
}

void UniverseWidget::SetIdaSyncAddress(
    const std::optional< u64 > address )
{
    if ( m_idaSyncAddress == address )
        return;

    m_idaSyncAddress = address;
    m_idaSyncIndex = CANTORDUST_NO_SOURCE;

    if ( m_graph != nullptr && address.has_value() )
    {
        for ( usize i = 0; i < m_graph->nodes.size(); ++i )
        {
            if ( m_graph->nodes[ i ].address == *address )
            {
                m_idaSyncIndex = i;
                break;
            }
        }
    }

    m_idaSyncPulsePhase = 0.0f;
    m_idaSyncAlpha = m_idaSyncIndex < CANTORDUST_NO_SOURCE ? 118u : 0u;
    RefreshPulseTimer();
    m_dirty = true;
    update();
}

void UniverseWidget::SetRenderStyle( const UniverseRenderStyle_s& style )
{
    m_renderStyle = style;
    m_dirty = true;
    update();
}

void UniverseWidget::SetShowAllLinks( const bool enabled )
{
    m_allLinks = enabled;
    m_dirty = true;
    update();
}

void UniverseWidget::SetIsOverview( const bool overview )
{
    m_isOverview = overview;
}

void UniverseWidget::SetOnFocus(
    std::function< void( u64 ) > callback )
{
    m_onFocus = std::move( callback );
}

void UniverseWidget::SetOnToggleBranch(
    std::function< void( u64 ) > callback )
{
    m_onToggleBranch = std::move( callback );
}

void UniverseWidget::SetOnResetFocus( std::function< void() > callback )
{
    m_onResetFocus = std::move( callback );
}

void UniverseWidget::ClearSelection()
{
    ++m_flashSerial;
    m_hasSelection = false;
    m_selectedAddress = 0;
    m_selectedIndex = CANTORDUST_NO_SOURCE;
    m_dirty = true;
    update();
}

void UniverseWidget::FlashFocusedCenter( const usize index )
{
    if ( m_graph == nullptr || index >= m_graph->nodes.size() )
    {
        return;
    }

    m_selectedIndex = index;
    m_selectedAddress = m_graph->nodes[ index ].address;
    m_hasSelection = false;

    const u32 serial = ++m_flashSerial;
    m_dirty = true;
    update();
    QTimer::singleShot( 240, this, [this, serial]() {
        if ( serial != m_flashSerial || m_hasSelection )
        {
            return;
        }

        m_selectedIndex = CANTORDUST_NO_SOURCE;
        m_selectedAddress = 0;
        m_dirty = true;
        update();
    } );
}

bool UniverseWidget::CenterOnAddress( const u64 address )
{
    if ( m_graph == nullptr )
    {
        return false;
    }

    for ( usize i = 0; i < m_graph->nodes.size(); ++i )
    {
        if ( m_graph->nodes[ i ].address != address )
        {
            continue;
        }

        m_view.panX = m_graph->nodes[ i ].x;
        m_view.panY = m_graph->nodes[ i ].y;
        m_view.panZ = m_graph->nodes[ i ].z;
        m_autoFraming = false;

        if ( m_graph->focusIndex == i )
        {
            FlashFocusedCenter( i );
        }
        else
        {
            m_selectedIndex = i;
            m_selectedAddress = address;
            m_hasSelection = true;
            ++m_flashSerial;
            m_dirty = true;
            update();
        }

        return true;
    }

    return false;
}

void UniverseWidget::SetOnHistoryBack( std::function< void() > callback )
{
    m_onHistoryBack = std::move( callback );
}

void UniverseWidget::SetOnHistoryForward( std::function< void() > callback )
{
    m_onHistoryForward = std::move( callback );
}

void UniverseWidget::RestoreCamera( const CubeCamera_s& camera,
    const UniverseView_s& view )
{
    if ( !std::isfinite( camera.yaw ) || !std::isfinite( camera.pitch ) ||
         !std::isfinite( camera.zoom ) || !std::isfinite( view.panX ) ||
         !std::isfinite( view.panY ) || !std::isfinite( view.panZ ) )
    {
        return;
    }

    m_camera.yaw = std::clamp( camera.yaw, -1000.0f, 1000.0f );
    m_camera.pitch = std::clamp( camera.pitch, -1.5f, 1.5f );
    m_camera.zoom = std::clamp( camera.zoom, 0.05f, 20.0f );
    m_view.panX = std::clamp( view.panX, -32.0f, 32.0f );
    m_view.panY = std::clamp( view.panY, -32.0f, 32.0f );
    m_view.panZ = std::clamp( view.panZ, -32.0f, 32.0f );
    m_autoFraming = false;
    m_dirty = true;
    update();
}

void UniverseWidget::ResetCamera()
{
    m_camera = {};
    m_autoFraming = true;
    m_view = m_graph != nullptr ? Universe_DefaultView( *m_graph ) :
        UniverseView_s{};

    m_dirty = true;
    update();
}

void UniverseWidget::ReframeToHubs()
{
    // Recenter and restore the default zoom while retaining user orientation.
    m_autoFraming = true;
    m_view = m_graph != nullptr ? Universe_DefaultView( *m_graph ) :
        UniverseView_s{};

    m_camera.zoom = 1.0f;
    m_dirty = true;
    update();
}

void UniverseWidget::SetOnHover(
    std::function< void( usize ) > callback )
{
    m_onHover = std::move( callback );
}

void UniverseWidget::SetOnSelect(
    std::function< void( u64 ) > callback )
{
    m_onSelect = std::move( callback );
}

void UniverseWidget::SetOnNavigate(
    std::function< void( u64 ) > callback )
{
    m_onNavigate = std::move( callback );
}

void UniverseWidget::RefreshPulseTimer()
{
    const bool animateSync = m_idaSyncIndex != CANTORDUST_NO_SOURCE;

    if ( !animateSync )
        m_idaSyncAlpha = CANTORDUST_UNIVERSE_SYNC_ALPHA_MAX;

    if ( m_highlightPulseTimer == nullptr )
        return;

    if ( animateSync || m_pathPulseActive )
    {
        if ( !m_highlightPulseTimer->isActive() )
            m_highlightPulseTimer->start();
    }
    else
    {
        m_highlightPulseTimer->stop();
    }
}

void UniverseWidget::UpdatePulses()
{
    bool changed = false;

    if ( m_idaSyncIndex != CANTORDUST_NO_SOURCE )
    {
        // Existing Follow IDA halo: fast, with a persistent low glow.
        m_idaSyncPulsePhase += kIdaSyncPulseRadiansPerTick;

        if ( m_idaSyncPulsePhase > CANTORDUST_TAU_F )
            m_idaSyncPulsePhase -= CANTORDUST_TAU_F;

        const f32 wave = 0.5f + 0.5f * sin( m_idaSyncPulsePhase );

        const f32 minimumAlpha = static_cast< f32 >(
            CANTORDUST_UNIVERSE_SYNC_ALPHA_MIN );
        const f32 alphaRange = static_cast< f32 >(
            CANTORDUST_UNIVERSE_SYNC_ALPHA_MAX -
            CANTORDUST_UNIVERSE_SYNC_ALPHA_MIN );
        const u8 next = static_cast< u8 >(
            minimumAlpha + wave * alphaRange );

        if ( next != m_idaSyncAlpha )
        {
            m_idaSyncAlpha = next;
            changed = true;
        }
    }

    if ( m_pathPulseActive )
    {
        // ~0.86 s per cycle: double the v58.3 route pulse speed, still
        // gentler than the independent Follow IDA halo animation.
        // 0..255 is merely a quantized animation level, NOT a graph mutation.
        m_pathPulsePhase += kRoutePulseRadiansPerTick;

        if ( m_pathPulsePhase > CANTORDUST_TAU_F )
            m_pathPulsePhase -= CANTORDUST_TAU_F;

        const f32 wave = 0.5f + 0.5f * sin( m_pathPulsePhase );
        const u8 next = static_cast< u8 >(
            wave * CANTORDUST_BYTE_MAX_F );

        if ( next != m_pathPulseLevel )
        {
            m_pathPulseLevel = next;
            changed = true;
        }
    }

    if ( !changed )
        return;

    m_dirty = true;
    update();
}

void UniverseWidget::EnsureImage()
{
    const int rasterWidth = std::clamp( width(), 1, 2048 );
    const int rasterHeight = std::clamp( height(), 1, 2048 );

    if ( !m_dirty && m_image.width == static_cast< usize >( rasterWidth ) &&
         m_image.height == static_cast< usize >( rasterHeight ) )
    {
        return;
    }

    static const UniverseGraph_s s_emptyGraph;
    Universe_RenderInto( m_image,
        m_graph != nullptr ? *m_graph : s_emptyGraph,
        m_camera, m_selectedIndex, m_allLinks,
        static_cast< usize >( rasterWidth ),
        static_cast< usize >( rasterHeight ), m_palette, m_view,
        // The timer advances these two values; render the *current* phase,
        // not the maximum intensities (which would make both pulses static).
        m_renderStyle, m_idaSyncIndex, m_idaSyncAlpha,
        m_pathPulseActive ?
            static_cast< f32 >( m_pathPulseLevel ) /
                CANTORDUST_BYTE_MAX_F : 0.0f );

    m_dirty = false;
}

usize UniverseWidget::HitTest( const QPoint& pos ) const
{
    if ( m_image.sourceIndex.empty() || width() <= 0 || height() <= 0 )
    {
        return CANTORDUST_NO_SOURCE;
    }

    const int px = static_cast< int >(
        static_cast< i64 >( pos.x() ) * m_image.width / width() );

    const int py = static_cast< int >(
        static_cast< i64 >( pos.y() ) * m_image.height / height() );

    const int radius = 7;
    int nearest = radius * radius + 1;
    usize best = CANTORDUST_NO_SOURCE;

    for ( int dy = -radius; dy <= radius; ++dy )
    {
        for ( int dx = -radius; dx <= radius; ++dx )
        {
            const int distance = dx * dx + dy * dy;

            if ( distance >= nearest || px + dx < 0 || py + dy < 0 )
            {
                continue;
            }

            const usize index = m_image.SourceAt(
                static_cast< usize >( px + dx ),
                static_cast< usize >( py + dy ) );

            if ( index != CANTORDUST_NO_SOURCE )
            {
                nearest = distance;
                best = index;
            }
        }
    }

    return best;
}

void UniverseWidget::paintEvent( QPaintEvent* )
{
    EnsureImage();
    QPainter painter( this );

    painter.fillRect( rect(), QColor::fromRgba(
        Palette_Get( m_palette ).background3d ) );

    if ( m_image.argb.empty() )
        return;

    const QImage bitmap(
        reinterpret_cast< const uchar* >( m_image.argb.data() ),
        static_cast< int >( m_image.width ),
        static_cast< int >( m_image.height ),
        static_cast< int >( m_image.width * sizeof( u32 ) ),
        QImage::Format_ARGB32 );

    painter.setRenderHint( QPainter::SmoothPixmapTransform, false );
    painter.drawImage( rect(), bitmap );
}

bool UniverseWidget::HasExportableGraph() const
{
    return m_graph != nullptr && !m_graph->nodes.empty();
}

bool UniverseWidget::SavePng( const QString& filename,
    const unsigned int longestSide ) const
{
    if ( !HasExportableGraph() || width() <= 0 || height() <= 0 ||
         longestSide < 128u || longestSide > 2048u )
    {
        return false;
    }

    const f64 ratio = static_cast< f64 >( longestSide ) /
        static_cast< f64 >( std::max( width(), height() ) );

    const usize imageWidth = static_cast< usize >(
        std::max( 1.0, std::round( width() * ratio ) ) );

    const usize imageHeight = static_cast< usize >(
        std::max( 1.0, std::round( height() * ratio ) ) );

    Image_s image;
    Universe_RenderInto( image, *m_graph, m_camera, m_selectedIndex,
        m_allLinks, imageWidth, imageHeight, m_palette, m_view,
        m_renderStyle, m_idaSyncIndex, m_idaSyncAlpha,
        static_cast< f32 >( m_pathPulseLevel ) /
            CANTORDUST_BYTE_MAX_F );

    if ( image.argb.empty() )
        return false;

    const QImage bitmap(
        reinterpret_cast< const uchar* >( image.argb.data() ),
        static_cast< int >( imageWidth ), static_cast< int >( imageHeight ),
        static_cast< int >( imageWidth * sizeof( u32 ) ),
        QImage::Format_ARGB32 );

    QSaveFile file( filename );

    if ( !file.open( QIODevice::WriteOnly ) ||
         !bitmap.save( &file, "PNG" ) )
    {
        return false;
    }

    return file.commit();
}

std::string UniverseWidget::ExportSvg( const unsigned int longestSide ) const
{
    if ( !HasExportableGraph() || width() <= 0 || height() <= 0 ||
         longestSide < 128u || longestSide > 8192u )
    {
        return {};
    }

    const f64 ratio = static_cast< f64 >( longestSide ) /
        static_cast< f64 >( std::max( width(), height() ) );

    const unsigned int outputWidth = static_cast< unsigned int >(
        std::max( 128.0, std::round( width() * ratio ) ) );

    const unsigned int outputHeight = static_cast< unsigned int >(
        std::max( 128.0, std::round( height() * ratio ) ) );

    return Universe_ExportSvg( *m_graph, m_camera, m_view,
        m_renderStyle, m_palette, m_allLinks, m_selectedIndex,
        outputWidth, outputHeight );
}

std::string UniverseWidget::ExportDot() const
{
    return HasExportableGraph() ? Universe_ExportDot( *m_graph ) : std::string{};
}

void UniverseWidget::resizeEvent( QResizeEvent* const event )
{
    m_dirty = true;
    QWidget::resizeEvent( event );
}

void UniverseWidget::mousePressEvent( QMouseEvent* const event )
{
    if ( event->button() == Qt::BackButton ||
         event->button() == Qt::ForwardButton )
    {
        if ( event->button() == Qt::BackButton && m_onHistoryBack )
        {
            m_onHistoryBack();
        }
        else if ( event->button() == Qt::ForwardButton && m_onHistoryForward )
        {
            m_onHistoryForward();
        }

        event->accept();
        return;
    }

    if ( event->button() != Qt::LeftButton &&
         event->button() != Qt::RightButton &&
         event->button() != Qt::MiddleButton )
    {
        QWidget::mousePressEvent( event );
        return;
    }

    setFocus( Qt::MouseFocusReason );
    m_pressedButton = event->button();
    m_panning = event->button() == Qt::RightButton ||
        event->button() == Qt::MiddleButton;

    m_dragging = true;
    m_rotated = false;
    m_lastPosition = event->pos();
    m_pressPosition = event->pos();
    setCursor( Qt::ClosedHandCursor );
    event->accept();
}

void UniverseWidget::mouseMoveEvent( QMouseEvent* const event )
{
    if ( m_dragging &&
         ( event->buttons().testFlag( Qt::LeftButton ) ||
           event->buttons().testFlag( Qt::RightButton ) ||
           event->buttons().testFlag( Qt::MiddleButton ) ) )
    {
        const QPoint delta = event->pos() - m_lastPosition;
        m_lastPosition = event->pos();

        if ( !delta.isNull() )
        {
            if ( m_panning )
            {
                m_autoFraming = false;

                const CubeTransform_s transform = Math_CubeTransform( m_camera );
                const f32 frame = m_graph != nullptr
                    ? Universe_FrameScale( *m_graph,
                        m_renderStyle.universeSpacing ) *
                        m_renderStyle.focusScale : 1.0f;

                const f32 magnitude = 1.8f / ( std::max( frame, 0.01f ) *
                    static_cast< f32 >( std::max( 120,
                        std::min( width(), height() ) ) ) );

                const f32 horizontal = -static_cast< f32 >( delta.x() ) * magnitude;
                const f32 vertical = static_cast< f32 >( delta.y() ) * magnitude;
                m_view.panX += horizontal * transform.cy +
                    vertical * transform.sy * transform.sp;

                m_view.panY += vertical * transform.cp;
                m_view.panZ -= horizontal * transform.sy -
                    vertical * transform.cy * transform.sp;
            }
            else
            {
                Cube_Rotate( m_camera, static_cast< f32 >( delta.x() ),
                    static_cast< f32 >( delta.y() ) );
            }

            m_rotated |= ( event->pos() - m_pressPosition ).manhattanLength() >= 4;
            m_dirty = true;
            update();
        }
    }
    else if ( m_onHover )
    {
        EnsureImage();
        m_onHover( HitTest( event->pos() ) );
    }

    event->accept();
}

void UniverseWidget::mouseReleaseEvent( QMouseEvent* const event )
{
    if ( event->button() == Qt::BackButton ||
         event->button() == Qt::ForwardButton )
    {
        event->accept();
        return;
    }

    if ( ( event->button() != Qt::LeftButton &&
           event->button() != Qt::RightButton &&
           event->button() != Qt::MiddleButton ) || !m_dragging )
    {
        QWidget::mouseReleaseEvent( event );
        return;
    }

    m_dragging = false;
    setCursor( Qt::CrossCursor );

    if ( m_isOverview && m_pressedButton == Qt::MiddleButton && !m_rotated &&
         m_graph != nullptr )
    {
        EnsureImage();
        const usize index = HitTest( event->pos() );

        if ( index < m_graph->nodes.size() )
        {
            const UniverseNode_s& node = m_graph->nodes[ index ];

            m_view.panX = node.x;
            m_view.panY = node.y;
            m_view.panZ = node.z;

            m_autoFraming = false;
            m_dirty = true;

            update();
        }
    }

    if ( !m_rotated && m_graph != nullptr )
    {
        EnsureImage();
        const usize index = HitTest( event->pos() );

        if ( m_pressedButton == Qt::RightButton )
        {
            if ( index < m_graph->nodes.size() )
            {
                m_selectedIndex = index;
                m_selectedAddress = m_graph->nodes[ index ].address;
                m_hasSelection = true;
                m_dirty = true;

                update();

                if ( m_onHover )
                    m_onHover( index );

                const u64 address = m_graph->nodes[ index ].address;
                ClearSelection();

                if ( !m_isOverview &&
                     event->modifiers().testFlag( Qt::ShiftModifier ) &&
                     m_graph->focusIndex != CANTORDUST_NO_SOURCE &&
                     m_onToggleBranch )
                {
                    m_onToggleBranch( address );
                }
                else if ( m_onFocus )
                    m_onFocus( address );
            }
            else if ( m_onResetFocus )
            {
                m_onResetFocus();
            }
        }
        else if ( m_pressedButton == Qt::LeftButton )
        {
            if ( index < m_graph->nodes.size() )
            {
                const u64 address = m_graph->nodes[ index ].address;
                const bool focusedCenter = m_graph->focusIndex == index;

                if ( focusedCenter )
                {
                    FlashFocusedCenter( index );
                }
                else
                {
                    m_selectedIndex = index;
                    m_selectedAddress = address;
                    m_hasSelection = true;
                    ++m_flashSerial;
                    m_dirty = true;
                    update();
                }

                if ( m_onHover )
                    m_onHover( index );

                // Inspect ONLY for Ctrl+left-click. Plain left-click retains
                // its pre-inspector callsite/navigation behavior.
                if ( event->modifiers().testFlag( Qt::ControlModifier ) )
                {
                    if ( m_onSelect )
                        m_onSelect( address );
                }
                else if ( m_onNavigate )
                {
                    m_onNavigate( address );
                }
            }
            else
            {
                m_selectedIndex = CANTORDUST_NO_SOURCE;
                m_hasSelection = false;
                m_dirty = true;

                update();
            }
        }
    }

    m_pressedButton = Qt::NoButton;
    event->accept();
}

void UniverseWidget::mouseDoubleClickEvent( QMouseEvent* const event )
{
    if ( event->button() == Qt::LeftButton )
    {
        if ( !m_isOverview && m_graph != nullptr &&
             m_graph->focusIndex < m_graph->nodes.size() )
        {
            // Qt sends a double-click press instead of a second normal
            // press. Keep that click available for call-site cycling.
            mousePressEvent( event );
            return;
        }

        m_dragging = false;
        m_pressedButton = Qt::NoButton;
        setCursor( Qt::CrossCursor );
        EnsureImage();

        const usize index = HitTest( event->pos() );

        if ( m_graph != nullptr && index < m_graph->nodes.size() )
        {
            // Center/pivot only; never open focus or advance call sites.
            CenterOnAddress( m_graph->nodes[ index ].address );
        }

        event->accept();
        return;
    }

    QWidget::mouseDoubleClickEvent( event );
}

void UniverseWidget::leaveEvent( QEvent* const event )
{
    if ( m_onHover )
        m_onHover( CANTORDUST_NO_SOURCE );

    QWidget::leaveEvent( event );
}

void UniverseWidget::wheelEvent( QWheelEvent* const event )
{
    m_wheelRemainder += event->angleDelta().y();

    const int steps = m_wheelRemainder / 120;
    m_wheelRemainder -= steps * 120;

    if ( steps != 0 )
    {
        // Extended zoom-out lets users navigate 10000% spacing.
        Cube_Zoom( m_camera, steps, 0.012f, 8.0f );
        m_autoFraming = false;
        m_dirty = true;
        update();
    }

    event->accept();
}

void UniverseWidget::keyPressEvent( QKeyEvent* const event )
{
    const CubeTransform_s transform = Math_CubeTransform( m_camera );
    const f32 frame = m_graph != nullptr
        ? Universe_FrameScale( *m_graph, m_renderStyle.universeSpacing ) *
            m_renderStyle.focusScale : 1.0f;

    const f32 step = ( event->modifiers().testFlag( Qt::ShiftModifier )
        ? 0.22f : 0.065f ) / std::max( frame, 0.01f );

    switch ( event->key() )
    {
        case Qt::Key_Left:
            if ( m_onHistoryBack )
                m_onHistoryBack();

            event->accept();
            return;

        case Qt::Key_Right:
            if ( m_onHistoryForward )
                m_onHistoryForward();

            event->accept();
            return;

        case Qt::Key_A:
            m_view.panX -= step * transform.cy;
            m_view.panZ += step * transform.sy;
            break;

        case Qt::Key_D:
            m_view.panX += step * transform.cy;
            m_view.panZ -= step * transform.sy;
            break;

        case Qt::Key_W:
            m_view.panX += step * transform.sy;
            m_view.panZ += step * transform.cy;
            break;

        case Qt::Key_S:
            m_view.panX -= step * transform.sy;
            m_view.panZ -= step * transform.cy;
            break;

        case Qt::Key_Q:
            m_view.panY -= step;
            break;

        case Qt::Key_E:
            m_view.panY += step;
            break;
        case Qt::Key_Period:
        case Qt::Key_Delete:
            if ( event->key() == Qt::Key_Period ||
                 event->modifiers().testFlag( Qt::KeypadModifier ) )
            {
                if ( m_graph != nullptr && m_selectedIndex < m_graph->nodes.size() )
                {
                    const UniverseNode_s& node = m_graph->nodes[ m_selectedIndex ];
                    m_view.panX = node.x;
                    m_view.panY = node.y;
                    m_view.panZ = node.z;
                    m_autoFraming = false;
                    m_dirty = true;
                    update();
                }

                event->accept();
                return;
            }

            QWidget::keyPressEvent( event );
            return;

        case Qt::Key_R:
            ResetCamera();
            event->accept();
            return;
        case Qt::Key_Escape:
            if ( m_onResetFocus )
                m_onResetFocus();

            event->accept();
            return;

        default:
            QWidget::keyPressEvent( event );
            return;
    }

    m_autoFraming = false;
    m_view.panX = std::clamp( m_view.panX, -32.0f, 32.0f );
    m_view.panY = std::clamp( m_view.panY, -32.0f, 32.0f );
    m_view.panZ = std::clamp( m_view.panZ, -32.0f, 32.0f );
    m_dirty = true;
    update();
    event->accept();
}

} // namespace cantordust
