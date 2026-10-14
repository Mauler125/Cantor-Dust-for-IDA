# Qt/IDA cannot be built in portable CI. Protect the production QWidget
# render call against accidentally hard-coding the two animated parameters.
# (The renderer's pixel behavior is tested separately in universe_tests.cpp.)
if(NOT DEFINED CANTORDUST_SOURCE_DIR)
    message(FATAL_ERROR "CANTORDUST_SOURCE_DIR is required")
endif()
file(READ "${CANTORDUST_SOURCE_DIR}/src/ida/universe_widget.cpp" widget)
string(FIND "${widget}" "void UniverseWidget::EnsureImage()" begin)
string(FIND "${widget}" "usize UniverseWidget::HitTest(" end)
if(begin EQUAL -1 OR end EQUAL -1 OR end LESS begin)
    message(FATAL_ERROR "Could not locate UniverseWidget::EnsureImage")
endif()
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${widget}" ${begin} ${length} render)
foreach(required IN ITEMS
    "m_renderStyle, m_idaSyncIndex, m_idaSyncAlpha"
    "static_cast< f32 >( m_pathPulseLevel ) /"
    "CANTORDUST_BYTE_MAX_F"
    "m_pathPulseActive ?")
    string(FIND "${render}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "EnsureImage lost live pulse parameter: ${required}")
    endif()
endforeach()
# Ensure only one renderer call is made per dirty frame.
string(REGEX MATCHALL "Universe_RenderInto\\(" calls "${render}")
list(LENGTH calls call_count)
if(NOT call_count EQUAL 1)
    message(FATAL_ERROR "Expected one widget frame rendering call")
endif()
