# Native IDA actions can be re-bound or cleared through IDA's shortcut editor.
# IDA and Qt cannot be instantiated in portable CI, so guard all integration
# pieces and avoid double-binding a second Qt shortcut for the same action.
file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/ida_plugin.cpp" _plugin)
file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/cantor_panel.h" _panel_header)
file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/cantor_panel_universe.cpp" _universe)
file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/cantor_panel_menus.cpp" _menus)
file(READ "${CANTORDUST_SOURCE_DIR}/README.md" _readme)

foreach(_required IN ITEMS
    "cantordust:set_path_start"
    "cantordust:set_path_end"
    "Ctrl+Alt+Shift+S"
    "Ctrl+Alt+Shift+E"
    "ACTION_DESC_LITERAL_PLUGMOD("
    "register_action( startDesc )"
    "register_action( endDesc )"
    "unregister_action( s_pathStartAction )"
    "unregister_action( s_pathEndAction )"
    "SetCallPathEndpointFromAction( isStart )"
    "AST_ENABLE_FOR_IDB")
    string(FIND "${_plugin}" "${_required}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Missing native IDA call-path binding: ${_required}")
    endif()
endforeach()

foreach(_required IN ITEMS
    "SetCallPathEndpointFromAction( bool isStart );")
    string(FIND "${_panel_header}" "${_required}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Missing public call-path action bridge: ${_required}")
    endif()
endforeach()
string(FIND "${_universe}" "SetUniversePathEndpoint( isStart );" _pos)
if(_pos EQUAL -1)
    message(FATAL_ERROR "Native shortcut must reuse ordinary path selection")
endif()

foreach(_required IN ITEMS
    "Configure &Shortcuts..."
    "process_ui_action( \"ShortcutEditor\" )")
    string(FIND "${_menus}" "${_required}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Missing shortcut configuration menu: ${_required}")
    endif()
endforeach()

string(FIND "${_plugin}" "QShortcut" _qt_shortcut)
# Only allow a mention in the comment explaining why we do not create Qt
# shortcuts. A second binding would double-trigger endpoints.
string(REGEX MATCH "new QShortcut|QShortcut\\(" _double_binding "${_plugin}")
if(_double_binding)
    message(FATAL_ERROR "The native IDA actions must not be duplicated by QShortcuts")
endif()
