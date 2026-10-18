# Protect the IDA-only QSettings defaults and palette-aware range overlays on
# headless CI systems where the proprietary IDA SDK cannot be compiled.
set(_root "${CANTORDUST_SOURCE_DIR}")

file(READ "${_root}/src/ida/cantor_panel.cpp" _panel)
file(READ "${_root}/src/ida/plot_widget.h" _plot_header)
file(READ "${_root}/src/ida/plot_widget.cpp" _plot)
file(READ "${_root}/src/ida/entropy_widget.cpp" _entropy)
file(READ "${_root}/src/ida/cantor_panel_render_navigation.cpp" _render)
file(READ "${_root}/src/ida/range_selection_style.h" _overlay)
file(READ "${_root}/src/ida/cantor_panel_anomalies.cpp" _anomalies)
file(READ "${_root}/src/math/anomaly_palette.h" _anomaly_palette)

foreach(_text IN ITEMS
    "kUniverseSpacingDefaultPercent = 1000"
    "settings.value( \"universeSpacing\", kUniverseSpacingDefaultPercent )"
    "settings.value( \"paletteDetail\", storedPalette )"
    "settings.value( \"paletteOverview\", storedPalette )"
    "static_cast< int >( CDP_REFERENCE )"
    "m_plot->SetPalette( CurrentPalette() )"
    "m_overviewPlot->SetPalette( CurrentOverviewPalette() )"
)
    string(FIND "${_panel}" "${_text}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "Missing panel palette/default wiring: ${_text}")
    endif()
endforeach()

foreach(_text IN ITEMS
    "void SetPalette( Palette_e palette );"
    "Palette_e m_palette = CDP_REFERENCE;")
    string(FIND "${_plot_header}" "${_text}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "Missing PlotWidget palette declaration: ${_text}")
    endif()
endforeach()

foreach(_widget IN ITEMS _plot _entropy)
    foreach(_text IN ITEMS
        "RangeSelection_Fill( m_palette )"
        "RangeSelection_Border( m_palette )"
        "Palette_Get( m_palette ).background")
        string(FIND "${${_widget}}" "${_text}" _at)
        if(_at EQUAL -1)
            message(FATAL_ERROR "Missing ${_widget} palette usage: ${_text}")
        endif()
    endforeach()
endforeach()

foreach(_text IN ITEMS
    "Palette_Get( palette ).selection"
    "color.setAlpha( 38 )"
    "color.setAlpha( 185 )")
    string(FIND "${_overlay}" "${_text}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "Missing palette overlay behavior: ${_text}")
    endif()
endforeach()

# Bookmarks intentionally suppress palette-box signals: rendering must still
# refresh each Qt overlay's palette directly.
foreach(_text IN ITEMS
    "m_plot->SetPalette( CurrentPalette() )"
    "m_overviewPlot->SetPalette( CurrentOverviewPalette() )"
    "m_entropyStrip->SetPalette( CurrentOverviewPalette() )")
    string(FIND "${_render}" "${_text}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "Missing render-time palette sync: ${_text}")
    endif()
endforeach()

# Preserve the user's finalized About heading and front-panel labels.
foreach(_text IN ITEMS
    "Support developer &#9825;</a>"
    "m_aboutDialog->setWindowTitle( \"About Cantor Dust\" )"
    "Created by Kawe Mazidjatari")
    string(FIND "${_panel}" "${_text}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "Missing approved UI text: ${_text}")
    endif()
endforeach()

# User-facing IDA text avoids typographic long dashes; use '-' instead.
file(GLOB _ida_sources "${_root}/src/ida/*.cpp")
foreach(_source IN LISTS _ida_sources)
    file(READ "${_source}" _source_content)
    foreach(_dash IN ITEMS "—" "–")
        string(FIND "${_source_content}" "${_dash}" _at)
        if(NOT _at EQUAL -1)
            message(FATAL_ERROR "Unexpected long dash in ${_source}")
        endif()
    endforeach()
endforeach()

# Anomaly rendering uses palette-derived category colors and passes detector
# kinds through the Detail and Overview marker maps. The explorer table also
# exposes the same palette colors with per-category icons.
foreach(_text IN ITEMS
    "Anomaly_PaletteColor( marker.kind, m_palette )"
    "Anomaly_OutlineColor( image.argb[ pixelIndex ] )"
    "SetAnomalyMarkers( std::vector< AnomalyMarker_s > markers )"
    "marker->kind")
    string(FIND "${_plot}" "${_text}" _at)
    if(_at EQUAL -1)
        if(_text MATCHES "SetAnomalyMarkers")
            string(FIND "${_plot_header}" "${_text}" _at)
        endif()
        if(_at EQUAL -1)
            message(FATAL_ERROR "Missing palette-colored anomaly rendering: ${_text}")
        endif()
    endif()
endforeach()
foreach(_text IN ITEMS
    "RefreshAnomalyPalette()"
    "Anomaly_PaletteColor( kind, palette )"
    "Anomaly_OutlineColor( Palette_Get( palette ).background )"
    "finding.kind")
    string(FIND "${_anomalies}" "${_text}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "Missing explorer anomaly palette mapping: ${_text}")
    endif()
endforeach()
foreach(_text IN ITEMS
    "case AnomalyKind_e::HighEntropy:"
    "case AnomalyKind_e::EntropyChange:"
    "case AnomalyKind_e::Padding:"
    "case AnomalyKind_e::DominantByte:"
    "case AnomalyKind_e::RepeatedPattern:")
    string(FIND "${_anomaly_palette}" "${_text}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "Missing category from anomaly color mapping: ${_text}")
    endif()
endforeach()
