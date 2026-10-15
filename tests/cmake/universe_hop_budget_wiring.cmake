# Ensure the native settings UI and status text retain the portable
# multi-hop expansion budgets even when IDA/Qt are unavailable in CI.
file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/cantor_panel.cpp" _panel)
file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/cantor_panel_render_navigation.cpp" _status)
file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/cantor_panel_bookmarks.cpp" _bookmarks)

foreach(_text IN ITEMS
    "CANTORDUST_UNIVERSE_HOP_MAX_FUNCTIONS"
    "CANTORDUST_UNIVERSE_HOP_MAX_LINKS"
    "CANTORDUST_UNIVERSE_HOP_DEFAULT_FUNCTIONS"
    "CANTORDUST_UNIVERSE_HOP_DEFAULT_LINKS"
    "explorerBudgetsV2"
    "explorerBudgetsV3"
    "storedNodeBudget == 7680"
    "storedLinkBudget == 35000"
    "storedNodeBudget == 768"
    "storedLinkBudget == 3500"
    "CANTORDUST_UNIVERSE_HOP_DEFAULT_FANOUT"
    "CANTORDUST_UNIVERSE_HOP_MAX_FANOUT"
    "explorerFanout"
    "Neighbors per function")
    string(FIND "${_panel}" "${_text}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Missing expanded hop settings wiring: ${_text}")
    endif()
endforeach()

string(FIND "${_status}" "m_universeFanoutEdit->value()" _status_position)
if(_status_position EQUAL -1)
    message(FATAL_ERROR "Hop truncation status does not match the new fanout")
endif()

foreach(_text IN ITEMS
    "CANTORDUST_UNIVERSE_HOP_DEFAULT_FUNCTIONS"
    "CANTORDUST_UNIVERSE_HOP_DEFAULT_LINKS"
    "m_universeFanoutEdit"
    "item.insert( \"fanout\""
    "item.value( \"fanout\"")
    string(FIND "${_bookmarks}" "${_text}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Bookmark default is out of sync: ${_text}")
    endif()
endforeach()
