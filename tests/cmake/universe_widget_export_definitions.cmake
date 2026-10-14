# Guard QWidget export entry points even when Qt and IDA are unavailable.
if(NOT DEFINED CANTORDUST_SOURCE_DIR)
    message(FATAL_ERROR "CANTORDUST_SOURCE_DIR is required")
endif()

file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/universe_widget.cpp" widget)

foreach(method IN ITEMS
    HasExportableGraph
    SavePng
    ExportSvg
    ExportDot)
    string(FIND "${widget}" "UniverseWidget::${method}(" found)

    if(found EQUAL -1)
        message(FATAL_ERROR
            "Missing UniverseWidget::${method} implementation (IDA linker error)")
    endif()
endforeach()
