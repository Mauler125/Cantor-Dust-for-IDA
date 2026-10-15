include_guard(GLOBAL)

function(Cantordust_ResolveSdkSource result sdkPath)
    if("${sdkPath}" STREQUAL "")
        message(FATAL_ERROR "IDA SDK path is empty")
    endif()

    cmake_path(SET _sdk NORMALIZE "${sdkPath}")
    cmake_path(ABSOLUTE_PATH _sdk NORMALIZE)

    if(EXISTS "${_sdk}/src/include/pro.h")
        cmake_path(APPEND _sdk "src")
    elseif(NOT EXISTS "${_sdk}/include/pro.h")
        message(FATAL_ERROR
            "Invalid IDA SDK: ${_sdk}. Expected include/pro.h under "
            "this directory or its src/ child.")
    endif()

    set(${result} "${_sdk}" PARENT_SCOPE)
endfunction()
