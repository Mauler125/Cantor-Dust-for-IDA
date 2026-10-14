# Prevent substitutions such as `long double` -> `long f64`.
# Portable builds do not compile the IDA/Qt integration, so scan source text.
file(GLOB_RECURSE _cantordust_sources LIST_DIRECTORIES FALSE
    "${CMAKE_CURRENT_LIST_DIR}/../../src/*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../src/*.h")

foreach(_source IN LISTS _cantordust_sources)
    file(READ "${_source}" _contents)

    if(_contents MATCHES "(long|short|signed|unsigned)[ \t\r\n]+(f32|f64)([^A-Za-z0-9_]|$)")
        message(FATAL_ERROR
            "Invalid compound floating alias in ${_source}; use long double "
            "or f32/f64 separately.")
    endif()
endforeach()
