# The proprietary Qt/IDA target is unavailable in portable CI. Guard the
# menu refactor's source connections and ensure menu commands still use the
# original QWidget signals/state rather than diverging copies.
set(_root "${CANTORDUST_SOURCE_DIR}")
file(READ "${_root}/src/ida/cantor_panel.cpp" _panel)
file(READ "${_root}/src/ida/cantor_panel.h" _header)
file(READ "${_root}/src/ida/cantor_panel_menus.cpp" _menus)
file(READ "${_root}/src/ida/cantor_panel_bookmarks.cpp" _exports)
file(READ "${_root}/CMakeLists.txt" _cmake)

foreach(_text IN ITEMS
    "root->setMenuBar( menuBar )"
    "BuildMenus( menuBar, visualPage, explorerPage,"
    "this, [this]() { RefreshData(); }"
    "this, [this]() { GoToIdaCursor(); }"
    "m_aboutDialog->setWindowTitle( \"About Cantor Dust\" )"
    "Open source under the MIT License.</p>"
    "Support developer &#9825;</a>"
    "m_paletteBox->hide()"
    "m_overviewPaletteBox->hide()"
    "searchLayout->addWidget( m_universeFilterToggle )"
    "searchLayout->addWidget( m_universeClusterToggle )")
    string(FIND "${_panel}" "${_text}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Panel menu integration missing: ${_text}")
    endif()
endforeach()

foreach(_text IN ITEMS
    "menuBar->addMenu( \"&File\" )"
    "menuBar->addMenu( \"&Edit\" )"
    "menuBar->addMenu( \"&View\" )"
    "menuBar->addMenu( \"&Navigate\" )"
    "menuBar->addMenu( \"&Tools\" )"
    "menuBar->addMenu( \"&Help\" )"
    "ExportUniverseGraph( UniverseExportFormat_e::Png )"
    "ExportUniverseGraph( UniverseExportFormat_e::Svg )"
    "ExportUniverseGraph( UniverseExportFormat_e::Dot )"
    "box->setCurrentIndex( index )"
    "action, &QAction::toggled"
    "checkbox, &QCheckBox::toggled"
    "&QMenu::aboutToShow"
    "ShowSettingsTab( filterPage )"
    "ShowSettingsTab( searchPage )"
    "GoToIdaCursor();"
    "RefreshData();"
    "m_universePathFromButton->click()"
    "m_universePathToButton->click()"
    "m_universeClearPathButton->click()"
    "m_aboutDialog->show()"
    "QDesktopServices::openUrl"
    "https://ko-fi.com/amos0")
    string(FIND "${_menus}" "${_text}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Menu action missing: ${_text}")
    endif()
endforeach()

foreach(_text IN ITEMS
    "enum class UniverseExportFormat_e { Choose, Png, Svg, Dot }"
    "void BuildMenus( QMenuBar* menuBar"
    "void RefreshData();"
    "void GoToIdaCursor();")
    string(FIND "${_header}" "${_text}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Menu declaration missing: ${_text}")
    endif()
endforeach()

foreach(_text IN ITEMS
    "format == UniverseExportFormat_e::Svg"
    "format == UniverseExportFormat_e::Dot"
    "format == UniverseExportFormat_e::Choose"
    "const QString chosenFormat"
    "QString suggestedName")
    string(FIND "${_exports}" "${_text}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Explicit export format missing: ${_text}")
    endif()
endforeach()

string(FIND "${_cmake}" "src/ida/cantor_panel_menus.cpp" _pos)
if(_pos EQUAL -1)
    message(FATAL_ERROR "Native CMake target omits menu implementation")
endif()
