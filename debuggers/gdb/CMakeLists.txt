project(gdb)

find_package(ZLIB)

function(add_debuggable_executable target)
    cmake_parse_arguments(add_debuggable_executable "" "" "SRCS" ${ARGN})
    add_executable(${target} ${add_debuggable_executable_SRCS})
    # force debug symbols for our debuggees, disable optimizations
    if (WIN32)
        set(_flags "/0d")
    else()
        set(_flags "-O0")
    endif()
    set_target_properties(${target} PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS_DEBUG} ${_flags}")
endfunction()

if (CMAKE_VERSION VERSION_GREATER "2.9" OR NOT CMAKE_GENERATOR MATCHES "Ninja")
    set(HAVE_PATH_WITH_SPACES_TEST TRUE)
else()
    message(WARNING "Disabling 'path with spaces' test, this CMake version would create a faulty build.ninja file. Upgrade to at least CMake v3.0")
endif()

add_subdirectory(unittests)
add_subdirectory(printers)

add_definitions( -DKDE_DEFAULT_DEBUG_AREA=9012 )

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    )

configure_file(config-ksysguard.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-ksysguard.h )

#add_subdirectory(tests)

########### next target ###############

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  ${KDE4_ENABLE_EXCEPTIONS}")

set(kdevgdb_PART_SRCS
    gdb.cpp
    debuggerplugin.cpp
    gdbcommandqueue.cpp
    gdbcommand.cpp
    gdbparser.cpp
    stty.cpp
    disassemblewidget.cpp
    memviewdlg.cpp
    gdboutputwidget.cpp
#    debuggertracingdialog.cpp
    breakpointcontroller.cpp
    mi/gdbmi.cpp
    mi/milexer.cpp
    mi/miparser.cpp
    stringhelpers.cpp
    debugsession.cpp
    gdblaunchconfig.cpp
    debugjob.cpp
    selectcoredialog.cpp
    variablecontroller.cpp
    gdbframestackmodel.cpp
    gdbvariable.cpp
    registers/registersview.cpp
    registers/registercontroller.cpp
    registers/registersmanager.cpp
    registers/registercontroller_x86.cpp
    registers/registercontroller_arm.cpp
    registers/modelsmanager.cpp
    registers/converters.cpp
    processselection.cpp
    ksysguard/process.cpp
    ksysguard/processes.cpp
    ksysguard/ksysguardprocesslist.cpp
    ksysguard/processes_atop_p.cpp
    ksysguard/processes_base_p.cpp
    ksysguard/processes_local_p.cpp
    ksysguard/processes_remote_p.cpp
    ksysguard/ProcessFilter.cpp
    ksysguard/ProcessModel.cpp
    ksysguard/ReniceDlg.cpp
)

set(kdevgdb_PART_UI
    debuggertracingdialog.ui
    debuggerconfigwidget.ui
    selectcore.ui
    selectaddress.ui
    registers/registersview.ui
    ksysguard/ProcessWidgetUI.ui
    ksysguard/ReniceDlgUi.ui
)

kde4_add_ui_files(kdevgdb_PART_SRCS ${kdevgdb_PART_UI})
kde4_add_plugin(kdevgdb ${kdevgdb_PART_SRCS})
target_link_libraries(kdevgdb ${KDEVPLATFORM_SUBLIME_LIBRARIES}
    ${KDEVPLATFORM_INTERFACES_LIBRARIES}
    ${KDEVPLATFORM_LANGUAGE_LIBRARIES}
    ${KDEVPLATFORM_DEBUGGER_LIBRARIES}
    ${KDEVPLATFORM_OUTPUTVIEW_LIBRARIES}
    ${KDEVPLATFORM_PROJECT_LIBRARIES}
    ${KDE4_KDEUI_LIBS}
    ${KDEVPLATFORM_UTIL_LIBRARIES}
    ${KDE4_KTEXTEDITOR_LIBS}
    ${KDE4WORKSPACE_PROCESSUI_LIBS}
    ${ZLIB_LIBRARY}
)

install(TARGETS kdevgdb DESTINATION ${PLUGIN_INSTALL_DIR})


########### next target ###############

set(gdbtest_PART_SRCS
    unittests/gdbtest.cpp
    gdb.cpp
    gdbcommandqueue.cpp
    gdbcommand.cpp
    gdbparser.cpp
    stty.cpp
    disassemblewidget.cpp
    memviewdlg.cpp
    gdboutputwidget.cpp
#    debuggertracingdialog.cpp
    breakpointcontroller.cpp
    mi/gdbmi.cpp
    mi/milexer.cpp
    mi/miparser.cpp
    stringhelpers.cpp
    debugsession.cpp
    variablecontroller.cpp
    gdbframestackmodel.cpp
    gdbvariable.cpp
    registers/registersview.cpp
    registers/registercontroller.cpp
    registers/registersmanager.cpp
    registers/registercontroller_x86.cpp
    registers/registercontroller_arm.cpp
    registers/modelsmanager.cpp
    registers/converters.cpp
    ksysguard/process.cpp
    ksysguard/processes.cpp
    ksysguard/ksysguardprocesslist.cpp
    ksysguard/processes_atop_p.cpp
    ksysguard/processes_base_p.cpp
    ksysguard/processes_local_p.cpp
    ksysguard/processes_remote_p.cpp
    ksysguard/ProcessFilter.cpp
    ksysguard/ProcessModel.cpp
    ksysguard/ReniceDlg.cpp
    )

kde4_add_ui_files(gdbtest_PART_SRCS
    debuggertracingdialog.ui
    selectaddress.ui
    registers/registersview.ui
    ksysguard/ProcessWidgetUI.ui
    ksysguard/ReniceDlgUi.ui
)
kde4_add_unit_test(gdbtest ${gdbtest_PART_SRCS})
target_link_libraries(gdbtest
    ${QT_QTTEST_LIBRARY}
    ${KDEVPLATFORM_SHELL_LIBRARIES}
    ${KDEVPLATFORM_INTERFACES_LIBRARIES}
    ${KDEVPLATFORM_PROJECT_LIBRARIES}
    ${KDEVPLATFORM_DEBUGGER_LIBRARIES}
    ${KDEVPLATFORM_TESTS_LIBRARIES}
    ${KDEVPLATFORM_UTIL_LIBRARIES}
    ${KDE4_KIO_LIBS}
    ${KDE4_KTEXTEDITOR_LIBS}
    ${KDE4_KPARTS_LIBRARY}
    ${KDE4WORKSPACE_PROCESSUI_LIBS}
    ${ZLIB_LIBRARY}
)

if (HAVE_PATH_WITH_SPACES_TEST)
    set_target_properties(gdbtest PROPERTIES COMPILE_FLAGS "-DHAVE_PATH_WITH_SPACES_TEST")
endif()

########### install files ###############

configure_file(kdevgdb.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/kdevgdb.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kdevgdb.desktop DESTINATION ${SERVICES_INSTALL_DIR})
install( FILES kdevgdbui.rc DESTINATION ${DATA_INSTALL_DIR}/kdevgdb )
