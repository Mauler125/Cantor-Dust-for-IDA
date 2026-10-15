# The Qt/IDA panel is excluded from portable builds. Check the interaction
# wiring so every range mutation reaches the same control and status refresh.
set(_root "${CANTORDUST_SOURCE_DIR}/src/ida")
file(READ "${_root}/cantor_panel_ranges.cpp" _ranges)
file(READ "${_root}/cantor_panel_render_navigation.cpp" _navigation)
file(READ "${_root}/cantor_panel_bookmarks.cpp" _bookmarks)
file(READ "${_root}/cantor_panel.h" _header)

function(_method out source start end)
    string(FIND "${source}" "${start}" _begin)
    if(_begin EQUAL -1)
        message(FATAL_ERROR "Missing method: ${start}")
    endif()
    string(SUBSTRING "${source}" ${_begin} -1 _tail)
    string(FIND "${_tail}" "${end}" _finish)
    if(_finish EQUAL -1)
        message(FATAL_ERROR "Missing end marker for method: ${start}")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_finish} _body)
    set(${out} "${_body}" PARENT_SCOPE)
endfunction()

function(_require body expected description)
    string(FIND "${body}" "${expected}" _index)
    if(_index EQUAL -1)
        message(FATAL_ERROR "Range sync regression: ${description}")
    endif()
endfunction()

_method(_filter "${_ranges}"
    "bool CantorPanel::eventFilter(" "void CantorPanel::WheelRangeLarge(")
_require("${_filter}" "WheelRangeLarge( steps );"
    "Horizontal wheel must call WheelRangeLarge")

_method(_horizontal "${_ranges}"
    "void CantorPanel::WheelRangeLarge(" "std::optional< u64 > CantorPanel::ResolveCursorUniverseAddress(")
_require("${_horizontal}" "m_detailSpan = next;"
    "Horizontal wheel must update the byte span")
_require("${_horizontal}" "SetWindowOffset( offset );"
    "Horizontal wheel must use shared navigation")

_method(_commit "${_ranges}"
    "void CantorPanel::CommitRangeText(" "bool CantorPanel::eventFilter(")
_require("${_commit}" "SetWindowOffset( m_windowOffset );"
    "Manual range editing must use shared navigation")

_method(_setwindow "${_navigation}"
    "void CantorPanel::SetWindowOffset(" "void CantorPanel::WheelDetail(")
_require("${_setwindow}" "Reload();"
    "Shared navigation must reload the canonical viewport")

_method(_reload "${_ranges}"
    "void CantorPanel::Reload(" "void CantorPanel::UpdateWindowInfoLabel(")
_require("${_reload}" "m_windowOffset = std::min("
    "Reload must clamp the requested start")
_require("${_reload}" "m_exactRequestedOffset.reset();"
    "Reload must finalize the requested start")
_require("${_reload}" "SyncRangeControl();"
    "Reload must synchronize the range field and address label")
_require("${_reload}" "m_loadedCount = loadedCount;"
    "Reload must retain loaded byte count for overview updates")
_require("${_reload}" "UpdateWindowInfoLabel();"
    "Reload must refresh the bottom debug status")
string(FIND "${_reload}" "m_exactRequestedOffset.reset();" _finalized)
string(FIND "${_reload}" "SyncRangeControl();" _synced)
string(FIND "${_reload}" "RenderCurrent();" _rendered)
if(NOT _finalized LESS _synced OR NOT _synced LESS _rendered)
    message(FATAL_ERROR "Range field must sync after offset clamp, before rendering")
endif()

_method(_sync "${_ranges}"
    "void CantorPanel::SyncRangeControl(" "void CantorPanel::CommitRangeText(")
_require("${_sync}" "m_rangeEdit->setText("
    "Range input must reflect final detail span")
_require("${_sync}" "UpdateRangeAddressLabel();"
    "Range address label must update with the input")
_require("${_sync}" "QSignalBlocker blocker( m_rangeEdit );"
    "Sync must not recursively commit edited text")

_method(_overview "${_navigation}"
    "void CantorPanel::WheelOverview(" "void CantorPanel::Navigate(")
_require("${_overview}" "UpdateWindowInfoLabel();"
    "Overview zoom must refresh bottom overview span without detail reload")

_method(_status "${_ranges}"
    "void CantorPanel::UpdateWindowInfoLabel(" "} // namespace cantordust")
_require("${_status}" "m_loadedCount"
    "Bottom status must use the last loaded count")
_require("${_status}" "m_overviewSpan"
    "Bottom status must show current overview span")
_require("${_status}" "m_detailSpan"
    "Bottom status must show current detail span")

_method(_reset "${_ranges}"
    "void CantorPanel::ResetEmpty(" "void CantorPanel::LoadOverview(")
_require("${_reset}" "SyncRangeControl();"
    "Empty segment must clear stale range field and address label")
_require("${_reset}" "m_loadedCount = 0;"
    "Empty segment must reset the loaded byte count")

_method(_bookmarks "${_bookmarks}"
    "bool CantorPanel::RestoreInvestigation(" "void CantorPanel::ExportUniverseGraph(")
_require("${_bookmarks}" "Reload();"
    "Bookmark restoration must use the shared sync path")

_require("${_header}" "void UpdateWindowInfoLabel();"
    "Panel must declare the shared status updater")
