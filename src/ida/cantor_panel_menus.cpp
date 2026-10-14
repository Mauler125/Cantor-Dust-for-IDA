#include "ida/cantor_panel.h"

#include <QAction>
#include <QActionGroup>
#include <QDesktopServices>
#include <QMenu>
#include <QMenuBar>
#include <QUrl>

namespace cantordust
{

void CantorPanel::RefreshData()
{
    m_universeLoaded = false;
    HideUniverseInspector();
    m_inspectorIndex = {};
    m_universeData = {};
    m_universeGraph = {};
    m_overviewFilteredGraph = {};
    m_detailUniverseGraph = {};
    m_universeFocusAddress.reset();
    m_universeHistory.Reset();
    m_lastCursorFunction.reset();
    m_universeFocusRangeToggle->setVisible( false );
    m_universeRangeHeatToggle->setVisible(
        m_viewBox->currentData().toInt() == CDV_UNIVERSE3D );
    m_callerClickOrdinals.clear();
    ListSegments( false );
    RebuildRanges();
    Reload();
}

void CantorPanel::GoToIdaCursor()
{
    if ( m_viewBox->currentData().toInt() == CDV_UNIVERSE3D )
    {
        CursorToUniverseFunction();
        return;
    }

    ListSegments( true );
    RebuildRanges();
    PositionAtCursor();
    Reload();
}

void CantorPanel::ShowSettingsTab( QWidget* const page )
{
    if ( page != nullptr )
        m_settingsTabs->setCurrentWidget( page );

    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void CantorPanel::BuildMenus( QMenuBar* const menuBar,
    QWidget* const displayPage, QWidget* const explorerPage,
    QWidget* const filterPage, QWidget* const searchPage )
{
    // Menus manipulate the SAME widgets and signals as the original panel.
    // Changing a palette, setting, or view from either surface must go
    // through the existing state/persistence and rendering paths.
    const auto addToggle = [this]( QMenu* const menu, const QString& title,
        QCheckBox* const checkbox ) {
        QAction* const action = menu->addAction( title );
        action->setCheckable( true );
        action->setChecked( checkbox->isChecked() );

        QObject::connect( action, &QAction::toggled,
            checkbox, &QCheckBox::setChecked );
        QObject::connect( checkbox, &QCheckBox::toggled,
            action, &QAction::setChecked );

        return action;
    };

    const auto addComboChoices = [this]( QMenu* const menu,
        QComboBox* const box ) {
        QActionGroup* const group = new QActionGroup( menu );
        group->setExclusive( true );

        for ( int index = 0; index < box->count(); ++index )
        {
            QAction* const action = menu->addAction(
                box->itemIcon( index ), box->itemText( index ) );
            action->setCheckable( true );
            action->setChecked( index == box->currentIndex() );
            group->addAction( action );
            action->setData( index );

            QObject::connect( action, &QAction::triggered,
                this, [box, index]() { box->setCurrentIndex( index ); } );
            QObject::connect( box,
                QOverload< int >::of( &QComboBox::currentIndexChanged ),
                action, [action, index]( const int current ) {
                    action->setChecked( current == index );
                } );
        }

        // Bookmark restores block combo-box signals deliberately. Reconcile
        // checkmarks when the menu opens, even after a silent restore.
        QObject::connect( menu, &QMenu::aboutToShow,
            menu, [menu, box]() {
                for ( QAction* const action : menu->actions() )
                    action->setChecked(
                        action->data().toInt() == box->currentIndex() );
            } );
    };

    QMenu* const fileMenu = menuBar->addMenu( "&File" );
    fileMenu->addAction( "Investigation &Bookmarks...", this,
        [this]() { ShowBookmarkManager(); } );
    fileMenu->addSeparator();
    QMenu* const exportMenu = fileMenu->addMenu( "&Export Universe" );
    exportMenu->addAction( "&PNG Image...", this, [this]() {
        ExportUniverseGraph( UniverseExportFormat_e::Png );
    } );
    exportMenu->addAction( "&SVG Vector Image...", this, [this]() {
        ExportUniverseGraph( UniverseExportFormat_e::Svg );
    } );
    exportMenu->addAction( "&Graphviz DOT...", this, [this]() {
        ExportUniverseGraph( UniverseExportFormat_e::Dot );
    } );
    exportMenu->addSeparator();
    exportMenu->addAction( "Choose &Format...", this, [this]() {
        ExportUniverseGraph();
    } );

    QMenu* const editMenu = menuBar->addMenu( "&Edit" );
    QMenu* const palettesMenu = editMenu->addMenu( "Color &Palettes" );
    addComboChoices( palettesMenu->addMenu( "&Overview" ), m_overviewPaletteBox );
    addComboChoices( palettesMenu->addMenu( "&Detail" ), m_paletteBox );
    palettesMenu->addSeparator();
    palettesMenu->addAction( "Use Overview Palette for &Both", this, [this]() {
        m_paletteBox->setCurrentIndex( m_overviewPaletteBox->currentIndex() );
    } );
    palettesMenu->addAction( "Use Detail Palette for Both", this, [this]() {
        m_overviewPaletteBox->setCurrentIndex( m_paletteBox->currentIndex() );
    } );
    editMenu->addSeparator();
    editMenu->addAction( "&Display Settings...", this, [this, displayPage]() {
        ShowSettingsTab( displayPage );
    } );

    QMenu* const viewMenu = menuBar->addMenu( "&View" );
    addComboChoices( viewMenu->addMenu( "&Overview Mode" ), m_overviewViewBox );
    addComboChoices( viewMenu->addMenu( "&Detail Mode" ), m_viewBox );
    viewMenu->addSeparator();
    addToggle( viewMenu, "Show &Overview", m_overviewToggle );
    addToggle( viewMenu, "Show &Entropy", m_entropyToggle );
    QMenu* const appearanceMenu = viewMenu->addMenu( "&Universe Appearance" );
    addToggle( appearanceMenu, "&Organic Layout", m_organicLayoutToggle );
    addToggle( appearanceMenu, "&Anti-aliasing", m_antiAliasingToggle );
    appearanceMenu->addSeparator();
    addToggle( appearanceMenu, "Relative Node &Heat", m_relativeNodeHeatToggle );
    addToggle( appearanceMenu, "Relative Node &Size", m_relativeNodeSizeToggle );
    QAction* const rangeHeatAction = addToggle( appearanceMenu,
        "Range-based Node Heat/Size", m_universeRangeHeatToggle );
    QObject::connect( appearanceMenu, &QMenu::aboutToShow,
        appearanceMenu, [this, rangeHeatAction]() {
            rangeHeatAction->setEnabled(
                m_viewBox->currentData().toInt() == CDV_UNIVERSE3D &&
                !m_universeFocusAddress.has_value() );
        } );
    appearanceMenu->addSeparator();
    addToggle( appearanceMenu, "Show &Globals", m_showGlobalsToggle );
    addToggle( appearanceMenu, "Count Unique &Callers", m_distinctCallersToggle );
    addToggle( appearanceMenu, "Rank Globals by Distinct &Fields", m_globalFieldsToggle );

    QMenu* const navigateMenu = menuBar->addMenu( "&Navigate" );
    navigateMenu->addAction( "Go to IDA &Cursor", this,
        [this]() { GoToIdaCursor(); } );
    navigateMenu->addAction( "&Refresh Data", this,
        [this]() { RefreshData(); } );
    navigateMenu->addSeparator();
    addToggle( navigateMenu, "&Follow IDA", m_followIdaToggle );
    navigateMenu->addSeparator();
    navigateMenu->addAction( "Focus &Back", this, [this]() {
        UniverseBack();
    } );
    navigateMenu->addAction( "Focus &Forward", this, [this]() {
        UniverseForward();
    } );
    navigateMenu->addAction( "&Clear Function Focus", this, [this]() {
        ClearUniverseFocus();
    } );

    QMenu* const toolsMenu = menuBar->addMenu( "&Tools" );
    QMenu* const searchMenu = toolsMenu->addMenu( "&Search && Graph" );
    addToggle( searchMenu, "&Filter View to Search", m_universeFilterToggle );
    addToggle( searchMenu, "Show Function &Clusters", m_universeClusterToggle );
    QAction* const rangeFocusAction = addToggle( searchMenu,
        "Focus &Callers in Range Only", m_universeFocusRangeToggle );
    QObject::connect( searchMenu, &QMenu::aboutToShow,
        searchMenu, [this, rangeFocusAction]() {
            rangeFocusAction->setEnabled(
                m_viewBox->currentData().toInt() == CDV_UNIVERSE3D &&
                m_universeFocusAddress.has_value() );
        } );
    searchMenu->addSeparator();
    searchMenu->addAction( "Search &Options...", this, [this, searchPage]() {
        ShowSettingsTab( searchPage );
    } );
    searchMenu->addAction( "&Advanced Graph Filters...", this, [this, filterPage]() {
        ShowSettingsTab( filterPage );
    } );
    QMenu* const pathsMenu = toolsMenu->addMenu( "&Call Paths" );
    pathsMenu->addAction( "Set Path &Start", this, [this]() {
        m_universePathFromButton->click();
    } );
    pathsMenu->addAction( "Set Path &End", this, [this]() {
        m_universePathToButton->click();
    } );
    pathsMenu->addAction( "&Clear Path", this, [this]() {
        m_universeClearPathButton->click();
    } );
    toolsMenu->addSeparator();
    toolsMenu->addAction( "&Explorer Budgets...", this, [this, explorerPage]() {
        ShowSettingsTab( explorerPage );
    } );
    toolsMenu->addAction( "&All Settings...", this, [this]() {
        ShowSettingsTab( nullptr );
    } );

    QMenu* const helpMenu = menuBar->addMenu( "&Help" );
    helpMenu->addAction( "&About Plugin...", this, [this]() {
        m_aboutDialog->show();
        m_aboutDialog->raise();
        m_aboutDialog->activateWindow();
    } );
    helpMenu->addAction( "&Source Code...", this, []() {
        QDesktopServices::openUrl( QUrl( QStringLiteral(
            "https://github.com/Mauler125/Cantor-Dust-for-IDA" ) ) );
    } );
    helpMenu->addSeparator();
    helpMenu->addAction( "&Donate...", this, []() {
        QDesktopServices::openUrl( QUrl( QStringLiteral(
            "https://ko-fi.com/amos0" ) ) );
    } );
}

} // namespace cantordust
