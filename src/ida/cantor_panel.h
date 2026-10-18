#ifndef IDA_CANTOR_PANEL_H
#define IDA_CANTOR_PANEL_H


#include "cantordust/scalar_types.h"
#include "ida/plot_widget.h"
#include "ida/cube_widget.h"
#include "ida/universe_widget.h"
#include "cantordust/universe.h"
#include "cantordust/palette.h"
#include "cantordust/renderer.h"
#include "ida/entropy_widget.h"
#include "math/ranges.h"
#include "math/anomaly.h"
#include "tier1/universe_history.h"

#include <QtWidgets>
#include <QMenuBar>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <ida.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cantordust
{

class CantorPanel final : public QWidget
{
public:
    explicit CantorPanel( QWidget* parent = nullptr );
    // Called by the IDA UI address-change hook (main/UI thread).
    void OnIdaCursorChanged( ea_t address );
    // Invoked by IDA-wide, user-configurable call-path actions.
    void SetCallPathEndpointFromAction( bool isStart );

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;
    void changeEvent( QEvent* event ) override;

private:
    bool IsAllSegments() const;
    usize Resolution() const;
    Palette_e CurrentPalette() const;
    Palette_e CurrentOverviewPalette() const;
    BytePlotOptions_s CurrentBytePlotOptions() const;
    usize WindowCapacity() const;
    void SyncRangeControl();
    void UpdateWindowInfoLabel();
    void ListSegments( bool followCursor );
    void RebuildRanges();
    void PositionAtCursor();
    void Reload();
    void ResetEmpty( const QString& message );
    void InvalidateOverviewSamples();
    void LoadOverview();
    void RenderCurrent();
    void LoadUniverse();
    void BuildUniverseCategory();
    void RefreshUniverseSearch();
    std::optional< u64 > SearchResultAddress() const;
    std::optional< UniversePathEndpoint_s > PathEndpointSelection() const;
    void SetUniversePathEndpoint( bool isStart );
    void UpdateUniversePathToolTips();
    void ShowUniversePath( u64 from, u64 to );
    void SelectUniverseRoute( int route );
    void OpenCommunity( u64 representative, bool overview );
    u64 VisibleCommunityAddress( u64 actual, bool overview ) const;
    void ClearUniverseRoutes();
    void HoverUniverse( usize index, bool overview );
    void SelectUniverseInspector( u64 address );
    void HideUniverseInspector();
    void UpdateUniverseInspector( u64 address );
    void ClearInspectorConnections();
    void AddInspectorMessage( const QString& message );
    void FocusUniverse( u64 address );
    void ToggleUniverseBranch( u64 address );
    void ClearUniverseFocus();
    void ApplyUniverseFocus( std::optional< u64 > address );
    void UniverseBack();
    void UniverseForward();
    std::vector< UniverseAddressRange_s > SelectedUniverseRanges() const;
    void CommitRangeText();
    void WheelRangeLarge( int steps );
    void CursorToUniverseFunction();
    std::optional< u64 > ResolveCursorUniverseAddress() const;
    void UpdateRangeAddressLabel();
    void RebuildDetailUniverse();
    void NavigateUniverseOverview( u64 address );
    void FollowIdaFunction();
    void LoadInvestigationBookmarks();
    bool SaveInvestigationBookmarks();
    QJsonObject CaptureInvestigation() const;
    bool RestoreInvestigation( const QJsonObject& bookmark );
    void ShowBookmarkManager();
    enum class UniverseExportFormat_e { Choose, Png, Svg, Dot };
    void ExportUniverseGraph( UniverseExportFormat_e format = UniverseExportFormat_e::Choose );
    void ShowSettingsTab( QWidget* page );
    void RefreshData();
    void ShowAnomalyExplorer();
    void ScanAnomalies();
    void ShowAnomalySettings();
    void ClearAnomalies();
    void RefreshAnomalyPalette();
    std::vector< AnomalyMarker_s > AnomalyMarkers(
        u64 start, u64 span, View_e mode ) const;
    void GoToAnomaly( u64 offset );
    void GoToIdaCursor();
    QWidget* SupplementalFeaturesForMode( View_e mode ) const;
    void UpdateSupplementalFeatures();
    void UpdateUniverseSearchLayout( int availableWidth );
    void UpdateSupportLink();
    void BuildMenus( QMenuBar* menuBar, QWidget* displayPage,
        QWidget* explorerPage, QWidget* filterPage, QWidget* searchPage );
    UniverseFilterOptions_s CurrentAdvancedFilters() const;
    void NavigateUniverse( u64 address );
    void NavigateUniverseCaller( u64 address );
    void RenderOverview();
    void UpdateOverviewVisibility();
    std::vector< QRect > BuildOverviewHighlights() const;
    void SetWindowOffset( u64 offset );
    void WheelOverview( usize source, usize ordinal,
        usize pixels, int steps );

    void WheelDetail( usize source, usize ordinal,
        usize pixels, int steps );

    void Navigate( usize index );
    void Hover( usize index );
    void NavigateOverview( usize offset );
    void HoverOverview( usize offset );

    QDialog* m_anomalyDialog = nullptr;
    QTableWidget* m_anomalyTable = nullptr;
    QLabel* m_anomalyStatus = nullptr;
    QPushButton* m_anomalyButton = nullptr;
    std::vector< AnomalyFinding_s > m_anomalies;
    bool m_anomalyScanned = false;
    AnomalyOptions_s m_anomalyOptions;
    QDialog* const m_aboutDialog;
    QDialog* const m_settingsDialog;
    QTabWidget* const m_settingsTabs;
    QCheckBox* const m_filterImports;
    QCheckBox* const m_filterThunks;
    QCheckBox* const m_filterLibraries;
    QCheckBox* const m_filterGlobals;
    QSpinBox* const m_filterMinimumIncoming;
    QSpinBox* const m_filterMinimumOutgoing;
    QCheckBox* const m_showGlobalsToggle;
    QCheckBox* const m_distinctCallersToggle;
    QCheckBox* const m_globalFieldsToggle;
    QComboBox* const m_segmentBox;
    QComboBox* const m_universeCategoryBox;
    QComboBox* const m_universeFocusModeBox;
    QSpinBox* const m_universeHopsEdit;
    QCheckBox* const m_followIdaToggle;
    QTimer* const m_idaFollowTimer;
    QSpinBox* const m_universeNodeBudgetEdit;
    QSpinBox* const m_universeLinkBudgetEdit;
    QSpinBox* const m_universeFanoutEdit;
    QWidget* const m_supplementalFeatures;
    QWidget* const m_cubeFeaturesPanel;
    QWidget* const m_bytePlotFeaturesPanel;
    QWidget* const m_histogramFeaturesRow;
    QWidget* const m_digraphFeaturesRow;
    QComboBox* const m_histogramScaleBox;
    QComboBox* const m_digraphIntensityBox;
    QSpinBox* const m_digraphDistanceEdit;
    QWidget* const m_universeExplorerPanel;
    QWidget* const m_universeSearchOptions;
    QWidget* const m_universeNeighborsControls;
    QWidget* const m_universeTierControls;
    QWidget* const m_universeWrapPanel;
    QWidget* const m_universePathControls;
    QLineEdit* const m_universeSearchEdit;
    QSpinBox* const m_universeMinRefsEdit;
    QCheckBox* const m_universeFilterToggle;
    QCheckBox* const m_universeClusterToggle;
    QComboBox* const m_universeSearchResults;
    QPushButton* const m_universePathFromButton;
    QPushButton* const m_universePathToButton;
    QPushButton* const m_universeClearPathButton;
    QComboBox* const m_universeRouteBox;
    QHBoxLayout* m_universeSearchRow = nullptr;
    QHBoxLayout* m_universeWrapRow = nullptr;
    QBoxLayout* m_universeOptionsLayout = nullptr;
    QBoxLayout* m_universeNeighborsLayout = nullptr;
    QBoxLayout* m_universeTierLayout = nullptr;
    bool m_universeOptionsWrapped = false;
    bool m_universeOptionsStacked = false;
    bool m_universeLabelsStacked = false;
    QComboBox* const m_viewBox;
    QComboBox* const m_overviewViewBox;
    QComboBox* const m_resolutionBox;
    QComboBox* const m_paletteBox;
    QComboBox* const m_overviewPaletteBox;
    QLineEdit* const m_rangeEdit;
    QCheckBox* const m_overviewToggle;
    QCheckBox* const m_entropyToggle;
    QCheckBox* const m_universeFocusRangeToggle;
    QCheckBox* const m_universeRangeHeatToggle;
    QCheckBox* const m_relativeNodeHeatToggle;
    QCheckBox* const m_relativeNodeSizeToggle;
    QCheckBox* const m_organicLayoutToggle;
    QCheckBox* const m_antiAliasingToggle;
    QComboBox* const m_cubeGeometryBox;
    QComboBox* const m_cubeValuesBox;
    QSpinBox* const m_cubeStrideEdit;
    QSpinBox* const m_cubeAlignmentEdit;
    QSpinBox* const m_cubeBitPlaneEdit;
    QSlider* const m_cubeSizeSlider;
    QSlider* const m_cubeBrightnessSlider;
    QCheckBox* const m_cubeAutoBrightnessToggle;
    QSlider* const m_universeSpacingSlider;
    QSpinBox* const m_universeSpacingEdit;
    QLabel* const m_infoLabel;
    QLabel* const m_rangeAddressLabel;
    QLabel* const m_supportLink;
    QLabel* const m_hintLabel;
    QFrame* const m_universeInspector;
    QLabel* const m_inspectorTitle;
    QLabel* const m_inspectorSummary;
    QScrollArea* const m_inspectorConnections;
    QWidget* const m_inspectorConnectionContent;
    QVBoxLayout* const m_inspectorConnectionLayout;
    QSplitter* const m_plotSplitter;
    EntropyWidget* const m_entropyStrip;
    PlotWidget* const m_overviewPlot;
    CubeWidget* const m_overviewCube;
    UniverseWidget* const m_overviewUniverse;
    QStackedWidget* const m_overviewStack;
    PlotWidget* const m_plot;
    CubeWidget* const m_cubePlot;
    UniverseWidget* const m_universePlot;
    QStackedWidget* const m_detailStack;

    QJsonArray m_investigationBookmarks;
    std::vector< ByteRange_s > m_ranges;
    u64 m_totalLength = 0;
    u64 m_windowOffset = 0;
    u64 m_detailSpan = 0;
    u64 m_overviewOffset = 0;
    u64 m_overviewSpan = 0;
    std::optional< u64 > m_exactRequestedOffset;
    usize m_byteCount = 0;
    usize m_loadedCount = 0;
    // A logical selection can be much larger than the image pixel budget.
    // For large selections the image is sampled across the entire span and
    // source indices are absolute logical offsets (not indices into m_cells).
    bool m_detailSampled = false;
    std::vector< usize > m_detailSampleOffsets;
    std::vector< ByteClassSummary_s > m_detailSampleClasses;
    std::vector< CubeTriplet_s > m_detailCubeTriplets;
    std::vector< ByteCell_s > m_cells;
    std::vector< ByteCell_s > m_overviewCells;
    std::vector< CubeTriplet_s > m_overviewTriplets;
    std::vector< ByteClassSummary_s > m_overviewClasses;
    std::vector< usize > m_overviewOffsets;
    bool m_overviewImageDirty = true;
    bool m_overviewCubeLoaded = false;
    bool m_universeLoaded = false;
    UniverseData_s m_universeData;
    UniverseInspectorIndex_s m_inspectorIndex;
    UniverseInspectorResult_s m_inspectorResult;
    std::optional< u64 > m_inspectorAddress;
    std::optional< u64 > m_lastHoveredAddress;
    bool m_lastHoverWasOverview = false;
    UniverseGraph_s m_universeGraph;
    UniverseGraph_s m_overviewFilteredGraph;
    std::unordered_map< u64, std::vector< u64 > > m_overviewCommunities;
    std::optional< u64 > m_lastCursorFunction;
    std::optional< u64 > m_idaFunctionAddress;
    UniverseGraph_s m_detailUniverseGraph;
    std::unordered_map< u64, std::vector< u64 > > m_detailCommunities;
    UniversePathEndpoints_s m_universePathEndpoints;
    bool m_pathPreferSearchResult = false;
    UniverseCallPath_s m_universeActivePath;
    std::vector< UniverseCallPath_s > m_universeRoutes;
    usize m_universeSelectedRoute = 0;
    int m_horizontalWheelRemainder = 0;
    std::optional< u64 > m_universeFocusAddress;
    std::unordered_set< u64 > m_universeCollapsedNodes;
    UniverseHistory m_universeHistory;
    std::unordered_map< u64, usize > m_callerClickOrdinals;
};

} // namespace cantordust

#endif // IDA_CANTOR_PANEL_H
