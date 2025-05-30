cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0115 NEW)
cmake_policy(SET CMP0135 NEW)

project(MyAppsStore VERSION 0.11)

set(EXTERNAL_DIR "" CACHE PATH "External dependencies folder")

execute_process(
    COMMAND "git" "rev-parse" "HEAD"
    WORKING_DIRECTORY  ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION ${VERSION}
    OUTPUT_STRIP_TRAILING_WHITESPACE)

set(PROJECT_VERSION_PATCH "${GIT_VERSION}")

message("project version: ${PROJECT_VERSION} - ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "release")
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE)

set(QTPREFIX_DIR "" CACHE PATH "QT Prefix directory")
set(CMAKE_CONFIGURATION_TYPES "release" CACHE STRING "" FORCE)
set(CONFIGURATION_TYPE "release")

message("project build type: ${CMAKE_BUILD_TYPE}")

if(CMAKE_BUILD_TYPE STREQUAL "debug")
    #force a single configuration type on Windows builds
    set(CMAKE_CONFIGURATION_TYPES "debug" CACHE STRING "" FORCE)
    set(CONFIGURATION_TYPE "debug")
else()
    #force a single configuration type on Windows builds
    set(CMAKE_CONFIGURATION_TYPES "release" CACHE STRING "" FORCE)
    set(CONFIGURATION_TYPE "release")
endif()

set(MYAPPS_FRONT_URL "front.myapps.directory:443" CACHE STRING "MyApps.dir Server Default URL")
set(EXTRA_DEFINITIONS "${EXTRA_DEFINITIONS}" CACHE STRING "Extra compiler definitions")
add_definitions(${EXTRA_DEFINITIONS})

###############################################################################
# Build configuration
###############################################################################
set(MYAPPS_CLIENT_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(MYAPPS_CLIENT_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(MYAPPS_CLIENT_VERSION_PATCH ${PROJECT_VERSION_PATCH})

###############################################################################
# Build external projects
###############################################################################

include(ExternalProject)

###############################################################################

if(EXTERNAL_DIR STREQUAL "")
    message(FATAL_ERROR "EXTERNAL DIR not specified.")
endif()

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)

find_package(Threads REQUIRED)

include(cmake/find-boost.cmake)

include(cmake/find-openssl.cmake)

include(cmake/find-solidframe.cmake)

include(cmake/build-cereal.cmake)

include(cmake/build-snappy.cmake)

include(cmake/build-config.cmake)

include(cmake/build-libzip.cmake)

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

if(LINUX)
    set(SYSTEM_BASIC_LIBRARIES rt)
    find_library(ATOMIC_LIBRARY NAMES atomic atomic.so.1 libatomic.so.1)

    if(ATOMIC_LIBRARY)
        message("Atomic library found: ${ATOMIC_LIBRARY}")
        list(APPEND SYSTEM_BASIC_LIBRARIES atomic)
    endif()

    set(SYSTEM_DYNAMIC_LOAD_LIBRARY dl)
endif(LINUX)

###############################################################################
include_directories(
    "${EXTERNAL_DIR}/include"
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_BINARY_DIR}/external/include"
)

link_directories(
    "${CMAKE_BINARY_DIR}/external"
    "${CMAKE_BINARY_DIR}"
    "${EXTERNAL_DIR}/lib"
    "${CMAKE_BINARY_DIR}/external/lib"
)

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )

###############################################################################
# OS Configuration
###############################################################################


###############################################################################
# cmake_config.hpp support
include(cmake/check.config.cmake)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.hpp.in ${CMAKE_BINARY_DIR}/myapps/client/client_config.hpp)

###############################################################################

###############################################################################
# C++ Standard
###############################################################################

set (CMAKE_CXX_STANDARD 20)

###############################################################################
# Static Analisys
###############################################################################

include(cmake/source-analisys.cmake)

###############################################################################
# ctest support
enable_testing()
include(CTest)
###############################################################################
add_subdirectory(myapps)

###############################################################################
include(cmake/source-format.cmake)
###############################################################################


set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "bin")
set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
set(CMAKE_INSTALL_DEBUG_LIBRARIES TRUE)

include(InstallRequiredSystemLibraries)

