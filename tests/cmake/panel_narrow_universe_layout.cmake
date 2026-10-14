# Source-level Qt wiring guard; the native IDA Qt GUI must still be tested.
set(_root "${CANTORDUST_SOURCE_DIR}")
file(READ "${_root}/src/ida/cantor_panel.cpp" _panel)
file(READ "${_root}/src/ida/cantor_panel.h" _header)
file(READ "${_root}/src/ida/cantor_panel_ranges.cpp" _ranges)
file(READ "${_root}/src/ida/cantor_panel_menus.cpp" _menus)

foreach(_required IN ITEMS
    "class FlexibleWidthPanel final : public QWidget"
    "m_universeSearchOptions( new FlexibleWidthPanel( m_universeExplorerPanel ) )"
    "m_universeWrapPanel( new FlexibleWidthPanel( m_universeExplorerPanel ) )"
    "m_universeNeighborsControls( new FlexibleWidthPanel( m_universeSearchOptions ) )"
    "m_universeTierControls( new FlexibleWidthPanel( m_universeSearchOptions ) )"
    "m_universeOptionsLayout->addWidget( m_universeNeighborsControls, 1 )"
    "m_universeOptionsLayout->addWidget( m_universeTierControls, 1 )"
    "m_universeNeighborsLayout->addWidget( m_universeFocusModeBox, 1 )"
    "m_universeTierLayout->addWidget( m_universeCategoryBox, 1 )"
    "neighborsLabel->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed )"
    "tierLabel->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed )"
    "m_universeCategoryBox->setSizePolicy(\n        QSizePolicy::Expanding, QSizePolicy::Fixed )"
    "m_universeFocusModeBox->setSizePolicy(\n        QSizePolicy::Expanding, QSizePolicy::Fixed )"
    "m_universeNeighborsControls->setSizePolicy("
    "m_universeTierControls->setSizePolicy("
    "m_universeWrapRow->addWidget( m_universeSearchOptions, 1 )"
    "m_universeSearchEdit->setMinimumWidth( 120 )"
    "Universe_SearchLayoutForWidth("
    "m_universeOptionsLayout->setDirection( stackOptions"
    "m_universeNeighborsLayout->setDirection( stackLabels"
    "m_universeTierLayout->setDirection( stackLabels"
    "if ( wrap )"
    "m_universeSearchRow->removeWidget( m_universeSearchOptions )"
    "m_universeWrapRow->addWidget( m_universeSearchOptions, 1 )"
    "m_universeWrapPanel->show()"
    "m_universeWrapRow->removeWidget( m_universeSearchOptions )"
    "m_universeSearchRow->addWidget( m_universeSearchOptions )"
    "m_universeWrapPanel->hide()"
    "m_universeSearchOptions->show()"
    "m_universeExplorerPanel->installEventFilter( this )"
    "parentWidget()->installEventFilter( this )"
    "parentWidget()->contentsRect().width()"
    "const bool wrap = layout.wrap || availableWidth > visibleWidth + 2"
    "m_universeSearchOptions->setMaximumWidth( wrap"
    "UpdateUniverseSearchLayout( m_universeExplorerPanel->width() )"
    "Support developer &#9825;</a>")
    string(FIND "${_panel}" "${_required}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Missing responsive Universe row behavior: ${_required}")
    endif()
endforeach()

foreach(_forbidden IN ITEMS
    "m_universeSearchOptions->setMinimumWidth( optionsWidth )"
    "m_universeWrapPanel->setMinimumWidth("
    "m_universeSearchOptions->setMinimumWidth("
    "m_universeCategoryBox->setMaximumWidth("
    "m_universeWrapRow->addStretch( 1 )")
    string(FIND "${_panel}" "${_forbidden}" _found)
    if(NOT _found EQUAL -1)
        message(FATAL_ERROR "Universe layout must not force an overflow width: ${_forbidden}")
    endif()
endforeach()

foreach(_required IN ITEMS
    "void UpdateUniverseSearchLayout( int availableWidth );"
    "QWidget* const m_universeNeighborsControls;"
    "QWidget* const m_universeTierControls;"
    "QBoxLayout* m_universeOptionsLayout = nullptr;"
    "QBoxLayout* m_universeNeighborsLayout = nullptr;"
    "QBoxLayout* m_universeTierLayout = nullptr;"
    "bool m_universeOptionsWrapped = false;"
    "bool m_universeOptionsStacked = false;"
    "bool m_universeLabelsStacked = false;")
    string(FIND "${_header}" "${_required}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Missing responsive Universe state: ${_required}")
    endif()
endforeach()

foreach(_required IN ITEMS
    "watched == m_universeExplorerPanel || watched == parentWidget()"
    "event->type() == QEvent::Resize"
    "event->type() == QEvent::FontChange"
    "UpdateUniverseSearchLayout( m_universeExplorerPanel->width() )")
    string(FIND "${_ranges}" "${_required}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Missing Universe resize event handling: ${_required}")
    endif()
endforeach()

foreach(_required IN ITEMS
    "Set Path &End"
    "&Source Code..."
    "https://github.com/Mauler125/Cantor-Dust-for-IDA"
    "m_universePathToButton->click()")
    string(FIND "${_menus}" "${_required}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Missing requested menu action: ${_required}")
    endif()
endforeach()

foreach(_removed IN ITEMS
    "3D &Byte Cloud Controls"
    "&Find Path to Function")
    string(FIND "${_menus}" "${_removed}" _found)
    if(NOT _found EQUAL -1)
        message(FATAL_ERROR "Obsolete menu action remains: ${_removed}")
    endif()
endforeach()
