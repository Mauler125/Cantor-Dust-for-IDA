# Qt publishes its namespace as a public target compile definition.
function(Cantordust_RequireQtNamespace target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "Missing Qt target: ${target}")
    endif()

    get_target_property(qtDefinitions ${target} INTERFACE_COMPILE_DEFINITIONS)
    if(NOT qtDefinitions OR NOT "QT_NAMESPACE=QT" IN_LIST qtDefinitions)
        message(FATAL_ERROR
            "Cantor Dust requires Qt built with -qtnamespace QT.\n"
            "${target} exports: ${qtDefinitions}\n"
            "Qt directory: ${Qt6_DIR}\n"
            "Use scripts/build-ida.ps1 (Windows) or scripts/build-ida.sh "
            "(Linux/macOS) with the namespaced Qt build.")
    endif()
    message(STATUS "CantorDust Qt namespace: QT (${target} target)")
endfunction()
