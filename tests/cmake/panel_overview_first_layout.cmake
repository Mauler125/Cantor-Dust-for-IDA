# Guard UI ordering and compact About / Universe layout in portable CI.
# The native Qt/IDA target cannot be built without the user's IDA SDK.
set(_root "${CANTORDUST_SOURCE_DIR}")
file(READ "${_root}/src/ida/cantor_panel.cpp" _panel)
file(READ "${_root}/src/ida/cantor_panel_menus.cpp" _menus)
file(READ "${_root}/src/ida/cantor_panel_bookmarks.cpp" _bookmarks)
file(READ "${_root}/src/ida/cantor_panel_ranges.cpp" _ranges)

function(require_order variable first second)
    string(FIND "${${variable}}" "${first}" _first)
    string(FIND "${${variable}}" "${second}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR NOT _first LESS _second)
        message(FATAL_ERROR "Incorrect UI order: ${first} must precede ${second}")
    endif()
endfunction()

require_order(_menus
    "palettesMenu->addMenu( \"&Overview\" )"
    "palettesMenu->addMenu( \"&Detail\" )")
require_order(_menus
    "viewMenu->addMenu( \"&Overview Mode\" )"
    "viewMenu->addMenu( \"&Detail Mode\" )")
require_order(_menus
    "Use Overview Palette for &Both"
    "Use Detail Palette for Both")
require_order(_panel
    "modeRow->addWidget( m_overviewViewBox, 1 )"
    "modeRow->addWidget( m_viewBox, 1 )")
require_order(_panel
    "m_universeSearchRow->addWidget( m_universeSearchEdit, 1 )"
    "m_universeNeighborsLayout->addWidget( m_universeFocusModeBox, 1 )")
require_order(_panel
    "m_universeNeighborsLayout->addWidget( m_universeFocusModeBox, 1 )"
    "m_universeTierLayout->addWidget( m_universeCategoryBox, 1 )")
require_order(_panel
    "m_universeTierLayout->addWidget( m_universeCategoryBox, 1 )"
    "exploreLayout->addLayout( m_universeSearchRow )")
require_order(_bookmarks
    "{ \"Overview Universe\", \"Detail Universe\" }"
    "overview = choice == \"Overview Universe\"")

foreach(_required IN ITEMS
    "aboutLabel->heightForWidth( labelWidth )"
    "aboutScreen->availableGeometry().height()"
    "m_aboutDialog->resize( aboutWidth, std::min( idealHeight, maxHeight ) )"
    "m_universeCategoryBox->setMinimumWidth( 80 )"
    "\"Neighbors:\", m_universeNeighborsControls )"
    "\"Tier:\", m_universeTierControls )"
    "Open source under the MIT License.</p>")
    string(FIND "${_panel}" "${_required}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Missing compact panel element: ${_required}")
    endif()
endforeach()

string(FIND "${_panel}" "m_aboutDialog->resize( 510, 500 )" _oldsize)
if(NOT _oldsize EQUAL -1)
    message(FATAL_ERROR "About dialog still uses the oversized hardcoded height")
endif()
string(FIND "${_panel}" "resultRow->addWidget( m_universeFocusModeBox )" _oldneighbors)
string(FIND "${_panel}" "resultRow->addWidget( m_universeCategoryBox )" _oldtier)
if(NOT _oldneighbors EQUAL -1 OR NOT _oldtier EQUAL -1)
    message(FATAL_ERROR "Neighbors or Tier still appear on the results row")
endif()
string(FIND "${_ranges}" "Overview %1 bytes | Detail %2 bytes" _status)
if(_status EQUAL -1)
    message(FATAL_ERROR "Status bar does not list Overview before Detail")
endif()
