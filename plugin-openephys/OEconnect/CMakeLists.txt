cmake_minimum_required(VERSION 3.20)
project(OEconnect LANGUAGES C CXX VERSION 0.0.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(OEC_PLUGIN_BUILD_TESTS  "Build OEconnect plugin unit tests" ON)

# NOTE: the loadable OE GUI plugin (OEconnect.dll) is NOT built here. This
# plugin-GUI version builds plugins inside the GUI CMake tree (link
# open-ephys.lib, depend on the `open-ephys` target). Build it via the
# two-stage flow in oe-gui-plugin/ (see oe-gui-plugin/CMakeLists.txt and
# build-oe-plugin.ps1). This file builds only liboeconnect + the unit tests.

# --- liboeconnect (sibling) ---
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../libshared/oeconnect
                 ${CMAKE_CURRENT_BINARY_DIR}/liboeconnect)

# --- libzmq + cppzmq via FetchContent ---
# libzmq v4.3.5 declares cmake_minimum_required < 3.5; CMake 4 needs explicit policy floor.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
include(FetchContent)
set(ZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_PERF_TOOL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(WITH_DOC OFF CACHE BOOL "" FORCE)
set(CPPZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libzmq
  GIT_REPOSITORY https://github.com/zeromq/libzmq.git
  GIT_TAG        v4.3.5
)
FetchContent_Declare(cppzmq
  GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
  GIT_TAG        v4.10.0
)
FetchContent_MakeAvailable(libzmq cppzmq)

# The loadable OE GUI plugin is built separately against a configured GUI tree
# (open-ephys.lib + the `open-ephys` target). See oe-gui-plugin/CMakeLists.txt.

if(OEC_PLUGIN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(Tests)
endif()
