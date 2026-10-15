# Portable CI cannot execute the IDA/Qt widget. Verify that both menu
# actions connect to order-independent endpoint selection and stale UI text
# does not reappear.
file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/cantor_panel.cpp" _panel)
file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/cantor_panel.h" _header)
file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/cantor_panel_universe.cpp" _universe)
file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/cantor_panel_menus.cpp" _menus)
file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/cantor_panel_bookmarks.cpp" _bookmarks)

foreach(_required IN ITEMS
    "new QPushButton( \"Set Path Start\""
    "new QPushButton( \"Set Path End\""
    "SetUniversePathEndpoint( true )"
    "SetUniversePathEndpoint( false )"
    "m_universePathEndpoints.Clear()"
    "UpdateUniversePathToolTips()")
    string(FIND "${_panel}" "${_required}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Missing path UI behavior: ${_required}")
    endif()
endforeach()

foreach(_required IN ITEMS
    "UniversePathEndpoints_s m_universePathEndpoints;"
    "void SetUniversePathEndpoint( bool isStart );")
    string(FIND "${_header}" "${_required}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Missing endpoint state: ${_required}")
    endif()
endforeach()

foreach(_required IN ITEMS
    "m_universePathEndpoints.SetStart( endpoint->address )"
    "m_universePathEndpoints.SetEnd( endpoint->address )"
    "UniversePathSelectionState_e::Waiting"
    "UniversePathSelectionState_e::SameFunction"
    "ShowUniversePath( *m_universePathEndpoints.start,"
    "ClearUniverseRoutes();"
    "RenderCurrent();")
    string(FIND "${_universe}" "${_required}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Missing call path selection flow: ${_required}")
    endif()
endforeach()

foreach(_required IN ITEMS
    "Set Path &Start"
    "Set Path &End")
    string(FIND "${_menus}" "${_required}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Call path menu action missing: ${_required}")
    endif()
endforeach()

foreach(_required IN ITEMS
    "m_universePathEndpoints.SetStart( startAddress )"
    "m_universePathEndpoints.SetEnd( endAddress )"
    "UpdateUniversePathToolTips();")
    string(FIND "${_bookmarks}" "${_required}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Bookmark endpoint restore missing: ${_required}")
    endif()
endforeach()

foreach(_text IN ITEMS "${_panel}" "${_universe}" "${_menus}")
    foreach(_obsolete IN ITEMS
        "Find path to"
        "Choose Path from first"
        "Path from")
        string(FIND "${_text}" "${_obsolete}" _pos)
        if(NOT _pos EQUAL -1)
            message(FATAL_ERROR "Obsolete call path text remains: ${_obsolete}")
        endif()
    endforeach()
endforeach()
