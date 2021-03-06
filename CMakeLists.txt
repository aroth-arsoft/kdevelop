# KDevelop version
set( KDEVELOP_VERSION_MAJOR 4 )
set( KDEVELOP_VERSION_MINOR 7 )
set( KDEVELOP_VERSION_PATCH 0 )
# KDevplatform dependency version
set( KDEVPLATFORM_VERSION "1.${KDEVELOP_VERSION_MINOR}.${KDEVELOP_VERSION_PATCH}" )

################################################################################

cmake_minimum_required(VERSION 2.8.9)

project(KDevelop)

set(CMAKE_MODULE_PATH ${KDevelop_SOURCE_DIR}/cmake/modules ${CMAKE_MODULE_PATH})

set(KDE_MIN_VERSION "4.7.0")
find_package(KDE4 4.7.0 REQUIRED)

include (KDE4Defaults)
include (MacroLibrary)
include (MacroOptionalAddSubdirectory)

find_package(QJSON)
# QJSON <=0.7.1 provides variables with lower case prefix, see bug 331426
if (QJSON_VERSION VERSION_LESS "0.7.2")
    set(QJSON_LIBRARIES   ${qjson_LIBRARIES})
    set(QJSON_INCLUDE_DIR ${qjson_INCLUDE_DIR})
endif()

# Make sure that we're having RPATH on our installed libs, else using kdevelop
# from prefixes like $HOME/kdevelop breaks
# Code taken from FindKDE4Internal.cmake from KDE 4.5
list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${LIB_INSTALL_DIR}"
  _isSystemLibDir)
if("${_isSystemLibDir}" STREQUAL "-1")
 set(CMAKE_INSTALL_RPATH "${LIB_INSTALL_DIR}")
endif("${_isSystemLibDir}" STREQUAL "-1")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

find_package(KDevPlatform ${KDEVPLATFORM_VERSION} REQUIRED)

include_directories(${KDEVPLATFORM_INCLUDE_DIR})

add_definitions (${QT_DEFINITIONS} ${KDE4_DEFINITIONS} -DHAVE_CONFIG_H=1)
add_definitions(-DQT_USE_FAST_CONCATENATION -DQT_USE_FAST_OPERATOR_PLUS)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # in Qt 4, clang support is lacking...
    add_definitions("-DQ_COMPILER_INITIALIZER_LISTS=1")
endif()

# Turn off missing-field-initializers warning to avoid noise from false positives with empty {}
# See discussion: http://mail.kde.org/pipermail/kdevelop-devel/2014-February/046910.html
if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-missing-field-initializers")
endif()

include_directories(${KDevelop_SOURCE_DIR} ${KDevelop_BINARY_DIR} ${KDE4_INCLUDES} )

# create config.h
configure_file (config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h )

add_subdirectory(cmake)
add_subdirectory(pics)
add_subdirectory(app)
add_subdirectory(formatters)
add_subdirectory(languages)
add_subdirectory(projectbuilders)
add_subdirectory(projectmanagers)
add_subdirectory(debuggers)
add_subdirectory(app_templates)
add_subdirectory(documentation)
add_subdirectory(kdeintegration)
add_subdirectory(utils)
add_subdirectory(file_templates)
add_subdirectory(providers)

macro_optional_add_subdirectory(doc)

macro_display_feature_log()

include(CTest)

# CTestCustom.cmake has to be in the CTEST_BINARY_DIR.
# in the KDE build system, this is the same as CMAKE_BINARY_DIR.
configure_file(${CMAKE_SOURCE_DIR}/CTestCustom.cmake ${CMAKE_BINARY_DIR}/CTestCustom.cmake)
