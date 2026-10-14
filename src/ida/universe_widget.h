#ifndef IDA_UNIVERSE_WIDGET_H
#define IDA_UNIVERSE_WIDGET_H


#include "cantordust/scalar_types.h"
#include "cantordust/universe.h"

#include <QPoint>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <cstdint>
#include <optional>
#include <string>

namespace cantordust
{

class UniverseWidget final : public QWidget
{
public:
    explicit UniverseWidget( QWidget* parent = nullptr );

    void SetGraph( const UniverseGraph_s* graph, Palette_e palette );
    void SetShowAllLinks( bool enabled );
    void SetRenderStyle( const UniverseRenderStyle_s& style );
    void SetIsOverview( bool overview );
    void SetIdaSyncAddress( std::optional< u64 > address );
    void SetOnFocus( std::function< void( u64 ) > callback );
    void SetOnToggleBranch( std::function< void( u64 ) > callback );
    void SetOnResetFocus( std::function< void() > callback );
    CubeCamera_s Camera() const { return m_camera; }
    UniverseView_s View() const { return m_view; }
    void RestoreCamera( const CubeCamera_s& camera, const UniverseView_s& view );
    bool HasExportableGraph() const;
    bool SavePng( const QString& filename, unsigned int longestSide = 2048u ) const;
    std::string ExportSvg( unsigned int longestSide = 2048u ) const;
    std::string ExportDot() const;
    void ResetCamera();
    void ReframeToHubs();
    void ClearSelection();
    bool CenterOnAddress( u64 address );
    void SetOnHistoryBack( std::function< void() > callback );
    void SetOnHistoryForward( std::function< void() > callback );
    void SetOnHover( std::function< void( usize ) > callback );
    void SetOnSelect( std::function< void( u64 ) > callback );
    void SetOnNavigate( std::function< void( u64 ) > callback );

protected:
    void paintEvent( QPaintEvent* event ) override;
    void resizeEvent( QResizeEvent* event ) override;
    void mousePressEvent( QMouseEvent* event ) override;
    void mouseMoveEvent( QMouseEvent* event ) override;
    void mouseReleaseEvent( QMouseEvent* event ) override;
    void mouseDoubleClickEvent( QMouseEvent* event ) override;
    void leaveEvent( QEvent* event ) override;
    void wheelEvent( QWheelEvent* event ) override;
    void keyPressEvent( QKeyEvent* event ) override;

private:
    void EnsureImage();
    void UpdatePulses();
    void RefreshPulseTimer();
    void FlashFocusedCenter( usize index );
    usize HitTest( const QPoint& pos ) const;

    const UniverseGraph_s* m_graph = nullptr;
    Palette_e m_palette = CDP_REFERENCE;
    UniverseRenderStyle_s m_renderStyle;
    CubeCamera_s m_camera;
    UniverseView_s m_view;
    Image_s m_image;
    usize m_selectedIndex = CANTORDUST_NO_SOURCE;
    u64 m_selectedAddress = 0;
    std::optional< u64 > m_idaSyncAddress;
    usize m_idaSyncIndex = CANTORDUST_NO_SOURCE;
    QTimer* m_highlightPulseTimer = nullptr;
    f32 m_idaSyncPulsePhase = 0.0f;
    u8 m_idaSyncAlpha = CANTORDUST_UNIVERSE_SYNC_ALPHA_MAX;
    f32 m_pathPulsePhase = 0.0f;
    u8 m_pathPulseLevel = 0u;
    u8 m_pulsingRoute = 0u;
    bool m_pathPulseActive = false;
    bool m_hasSelection = false;
    std::function< void( usize ) > m_onHover;
    std::function< void( u64 ) > m_onSelect;
    std::function< void( u64 ) > m_onNavigate;
    std::function< void( u64 ) > m_onFocus;
    std::function< void( u64 ) > m_onToggleBranch;
    std::function< void() > m_onResetFocus;
    std::function< void() > m_onHistoryBack;
    std::function< void() > m_onHistoryForward;
    QPoint m_lastPosition;
    QPoint m_pressPosition;
    int m_wheelRemainder = 0;
    bool m_dragging = false;
    bool m_rotated = false;
    bool m_panning = false;
    Qt::MouseButton m_pressedButton = Qt::NoButton;
    bool m_allLinks = false;
    bool m_isOverview = false;
    bool m_autoFraming = true;
    bool m_dirty = true;
    u32 m_flashSerial = 0;
};

} // namespace cantordust

#endif // IDA_UNIVERSE_WIDGET_H
