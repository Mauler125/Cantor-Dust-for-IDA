# The native Qt/IDA panel is not compiled in portable CI. Verify that the
# tested range-containment policy is actually connected to the panel's
# navigation, range commit and cache invalidation paths.
set(_root "${CANTORDUST_SOURCE_DIR}")
file(READ "${_root}/src/ida/cantor_panel_ranges.cpp" _ranges)
file(READ "${_root}/src/ida/cantor_panel_render_navigation.cpp" _render)

foreach(_part IN ITEMS
    "Math_OverviewContainingDetail("
    "SetWindowOffset( detailStart )"
    "SetWindowOffset( m_windowOffset )")
    string(FIND "${_ranges}" "${_part}" _index)
    if(_index EQUAL -1)
        message(FATAL_ERROR "Detail range commit is not synchronized: ${_part}")
    endif()
endforeach()

foreach(_part IN ITEMS
    "void CantorPanel::InvalidateOverviewSamples()"
    "m_overviewCells.clear();"
    "m_overviewTriplets.clear();"
    "m_overviewCubeLoaded = false;"
    "m_overviewClasses.clear();"
    "m_overviewOffsets.clear();"
    "m_overviewImageDirty = true;"
    "Math_OverviewContainingDetail("
    "InvalidateOverviewSamples();"
    "Reload();")
    string(FIND "${_render}" "${_part}" _index)
    if(_index EQUAL -1)
        message(FATAL_ERROR "Overview viewport/cache wiring is incomplete: ${_part}")
    endif()
endforeach()
