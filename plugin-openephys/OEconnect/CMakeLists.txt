cmake_minimum_required(VERSION 3.20)
project(OEconnect LANGUAGES C CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(OEC_PLUGIN_BUILD_TESTS  "Build OEconnect plugin unit tests" ON)
option(OEC_PLUGIN_BUILD_BUNDLE "Build OEconnect JUCE plugin bundle (requires all Source/*.cpp)" OFF)

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
FetchContent_Declare(libzmq
  GIT_REPOSITORY https://github.com/zeromq/libzmq.git
  GIT_TAG        v4.3.5
)
FetchContent_Declare(cppzmq
  GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
  GIT_TAG        v4.10.0
)
FetchContent_MakeAvailable(libzmq cppzmq)

# --- OE plugin-build template ---
set(GUI_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/plugin-GUI)
include(${GUI_BASE_DIR}/CMake/PluginCommon.cmake OPTIONAL RESULT_VARIABLE OEPLUGIN_FOUND)

if(NOT OEPLUGIN_FOUND)
  message(WARNING
    "OE plugin SDK not found at ${GUI_BASE_DIR}. "
    "Submodule init required: git submodule update --init --recursive")
endif()

if(OEC_PLUGIN_BUILD_BUNDLE)
  set(OEC_PLUGIN_SOURCES
    Source/OEconnectProcessor.cpp        # pure-C++ hot path
    Source/OEconnectJuceProcessor.cpp    # JUCE GenericProcessor wrapper
    Source/OEconnectEditor.cpp
    Source/Transport/ShmemTransport.cpp
    Source/Transport/ZmqTransport.cpp
    Source/Boards/RhdAcqBoardAdapter.cpp
    Source/Boards/OnixAdapter.cpp
    Source/Boards/NeuropixelsAdapter.cpp
    Source/Boards/FileReaderAdapter.cpp
    Source/Sync/DriftEmitter.cpp
    Source/Util/SlowCmdWorker.cpp
  )

  add_library(OEconnect MODULE ${OEC_PLUGIN_SOURCES})
  target_include_directories(OEconnect
    PRIVATE Source
            ${GUI_BASE_DIR}/Source/Plugins
            ${GUI_BASE_DIR}/JuceLibraryCode
  )
  target_link_libraries(OEconnect
    PRIVATE oeconnect libzmq-static cppzmq
  )
  set_target_properties(OEconnect PROPERTIES
    PREFIX ""
    BUNDLE TRUE
    BUNDLE_EXTENSION "bundle"
    OUTPUT_NAME "OEconnect"
  )
endif()

if(OEC_PLUGIN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(Tests)
endif()
