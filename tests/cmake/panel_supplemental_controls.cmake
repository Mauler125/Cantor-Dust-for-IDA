# The native Qt/IDA target cannot run in portable CI. Guard against losing
# mode-specific controls or theme refresh wiring during panel reorganizations.
set(_root "${CANTORDUST_SOURCE_DIR}")
file(READ "${_root}/src/ida/cantor_panel.cpp" _panel)
file(READ "${_root}/src/ida/cantor_panel.h" _header)
file(READ "${_root}/src/ida/cantor_panel_menus.cpp" _menus)
file(READ "${_root}/src/ida/cantor_panel_bookmarks.cpp" _bookmarks)

foreach(_text IN ITEMS
    "new QWidget( m_supplementalFeatures )"
    "root->addWidget( m_supplementalFeatures )"
    "m_cubeGeometryBox->addItems"
    "m_cubeValuesBox->addItems"
    "cubeAppearanceRow->addWidget( m_cubeSizeSlider, 1 )"
    "cubeAppearanceRow->addWidget( m_cubeBrightnessSlider, 1 )"
    "cubeAppearanceRow->addWidget( resetCube )"
    "QSettings( \"CantorDust\", \"IDA\" ).setValue( \"cubeValues\", value )"
    "preferences.setValue( \"cubeGeometry\""
    "preferences.setValue( \"cubePointSize\""
    "preferences.setValue( \"cubeBrightness\""
    "case CDV_CUBE3D:"
    "return m_cubeFeaturesPanel;"
    "case CDV_UNIVERSE3D:"
    "return m_universeExplorerPanel;"
    "return nullptr;"
    "m_cubeFeaturesPanel->setVisible( showCube )"
    "m_universeExplorerPanel->setVisible( showUniverse )"
    "m_supplementalFeatures->setVisible( showCube || showUniverse )"
    "QPalette::WindowText"
    "QPalette::Link"
    "ColorContrast( candidate, background )"
    "QEvent::PaletteChange"
    "QEvent::ApplicationPaletteChange"
    "UpdateSupportLink();"
    "Support developer &#9825;</a>")
    string(FIND "${_panel}" "${_text}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Missing mode features or theme wiring: ${_text}")
    endif()
endforeach()

string(FIND "${_panel}" "settingsTabs->addTab( cubePage" _pos)
if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "3D Byte controls are still inside All Settings")
endif()

foreach(_text IN ITEMS
    "QWidget* SupplementalFeaturesForMode( View_e mode ) const;"
    "void UpdateSupplementalFeatures();"
    "void UpdateSupportLink();"
    "void changeEvent( QEvent* event ) override;"
    "QWidget* const m_cubeFeaturesPanel;"
    "QLabel* const m_supportLink;")
    string(FIND "${_header}" "${_text}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Missing panel declaration: ${_text}")
    endif()
endforeach()

string(FIND "${_menus}" "ShowSettingsTab( cubePage )" _pos)
if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "View menu still opens removed settings page")
endif()
string(FIND "${_menus}" "3D &Byte Cloud Controls" _pos)
if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "View menu still exposes redundant 3D Byte Controls")
endif()

string(FIND "${_bookmarks}" "UpdateSupplementalFeatures();" _pos)
if(_pos EQUAL -1)
    message(FATAL_ERROR "Bookmark restore can desynchronize mode-specific controls")
endif()
