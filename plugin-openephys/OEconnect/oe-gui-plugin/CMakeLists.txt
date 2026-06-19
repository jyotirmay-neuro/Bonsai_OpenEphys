# OEconnect — Open Ephys GUI plugin build (stage 2).
#
# This plugin-GUI version (1.0.x) builds plugins *inside the GUI CMake tree*:
# each plugin links open-ephys.lib and uses the in-tree `open-ephys` target
# (external/plugin-GUI/Plugins/PluginRules.cmake). Rather than rebuild the GUI
# inside our configure, we use the standard two-stage flow:
#
#   Stage 1  build the GUI once:
#     cmake -S external/plugin-GUI -B external/plugin-GUI/Build -A x64
#     cmake --build external/plugin-GUI/Build --config Release --target open-ephys
#
#   Stage 2  build this plugin against the GUI's headers + open-ephys.lib:
#     cmake -S plugin-openephys/OEconnect/oe-gui-plugin -B <build> -A x64 \
#           -DOPEN_EPHYS_LIB=<...>/external/plugin-GUI/Build/Release/open-ephys.lib
#     cmake --build <build> --config Release
#
# See build-oe-plugin.ps1 (sibling) which automates both stages.

cmake_minimum_required(VERSION 3.20)
project(OEconnectOEPlugin LANGUAGES C CXX VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(OEC_ROOT  ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
get_filename_component(REPO_ROOT ${OEC_ROOT}/../.. ABSOLUTE)
set(GUI_BASE_DIR ${OEC_ROOT}/external/plugin-GUI CACHE PATH "Open Ephys plugin-GUI source root")
set(JUCE_DIRECTORY ${GUI_BASE_DIR}/JuceLibraryCode)

# Path to open-ephys.lib produced by the stage-1 GUI build (required on MSVC,
# where the plugin imports JUCE symbols exported by the open-ephys executable).
set(OPEN_EPHYS_LIB "" CACHE FILEPATH "Path to open-ephys.lib from a built GUI (MSVC)")

if(NOT EXISTS ${GUI_BASE_DIR}/CMakeLists.txt)
  message(FATAL_ERROR
    "plugin-GUI submodule not initialized at ${GUI_BASE_DIR}. "
    "Run: git submodule update --init --recursive")
endif()
if(NOT EXISTS ${JUCE_DIRECTORY}/JuceHeader.h)
  message(FATAL_ERROR
    "JuceHeader.h not found. Configure the GUI first (stage 1) so it is generated.")
endif()

# --- native protocol lib (static, embedded into the plugin DLL) ---
set(OEC_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(OEC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(${REPO_ROOT}/libshared/oeconnect ${CMAKE_BINARY_DIR}/liboeconnect)

# --- libzmq + cppzmq (static) ---
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
include(FetchContent)
set(ZMQ_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WITH_PERF_TOOL     OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED       OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC       ON  CACHE BOOL "" FORCE)
set(WITH_DOC           OFF CACHE BOOL "" FORCE)
set(CPPZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libzmq GIT_REPOSITORY https://github.com/zeromq/libzmq.git GIT_TAG v4.3.5)
FetchContent_Declare(cppzmq GIT_REPOSITORY https://github.com/zeromq/cppzmq.git GIT_TAG v4.10.0)
FetchContent_MakeAvailable(libzmq cppzmq)

# --- OEconnect plugin ---
add_library(OEconnect SHARED
  ${OEC_ROOT}/OpenEphysLib.cpp
  ${OEC_ROOT}/Source/OEconnectProcessor.cpp
  ${OEC_ROOT}/Source/OEconnectJuceProcessor.cpp
  ${OEC_ROOT}/Source/OEconnectEditor.cpp
  ${OEC_ROOT}/Source/Transport/ShmemTransport.cpp
  ${OEC_ROOT}/Source/Transport/ZmqTransport.cpp
  ${OEC_ROOT}/Source/Boards/RhdAcqBoardAdapter.cpp
  ${OEC_ROOT}/Source/Boards/OnixAdapter.cpp
  ${OEC_ROOT}/Source/Boards/NeuropixelsAdapter.cpp
  ${OEC_ROOT}/Source/Boards/FileReaderAdapter.cpp
  ${OEC_ROOT}/Source/Sync/DriftEmitter.cpp
  ${OEC_ROOT}/Source/Util/SlowCmdWorker.cpp
)
set_target_properties(OEconnect PROPERTIES PREFIX "" OUTPUT_NAME "OEconnect")

target_include_directories(OEconnect PRIVATE
  ${OEC_ROOT}/Source
  ${GUI_BASE_DIR}/Plugins/Headers
  ${JUCE_DIRECTORY}
  ${JUCE_DIRECTORY}/modules)

target_compile_definitions(OEconnect PRIVATE
  OEPLUGIN
  "$<$<PLATFORM_ID:Windows>:JUCE_API=__declspec(dllimport)>"
  OEC_PLUGIN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  OEC_PLUGIN_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  OEC_PLUGIN_VERSION_PATCH=${PROJECT_VERSION_PATCH})

target_compile_features(OEconnect PRIVATE cxx_std_17)
target_link_libraries(OEconnect PRIVATE oeconnect libzmq-static cppzmq)

if(MSVC)
  if(NOT OPEN_EPHYS_LIB OR NOT EXISTS ${OPEN_EPHYS_LIB})
    message(FATAL_ERROR
      "Set -DOPEN_EPHYS_LIB=<path to open-ephys.lib> from the stage-1 GUI build.")
  endif()
  target_link_libraries(OEconnect PRIVATE ${OPEN_EPHYS_LIB})
  target_compile_options(OEconnect PRIVATE /sdl- /W0 /bigobj)
elseif(LINUX OR (UNIX AND NOT APPLE))
  target_link_libraries(OEconnect GL X11 Xext Xinerama asound dl freetype pthread rt)
  set_property(TARGET OEconnect APPEND_STRING PROPERTY LINK_FLAGS
    "-fvisibility=hidden -fPIC -rdynamic")
  target_compile_options(OEconnect PRIVATE -fPIC -rdynamic -O3)
elseif(APPLE)
  set_property(TARGET OEconnect APPEND_STRING PROPERTY LINK_FLAGS
    "-fPIC -rdynamic -undefined dynamic_lookup")
  target_link_libraries(OEconnect dl)
endif()
