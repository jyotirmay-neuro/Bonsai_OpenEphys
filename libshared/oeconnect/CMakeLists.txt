cmake_minimum_required(VERSION 3.20)
project(oeconnect LANGUAGES C VERSION 1.0.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(OEC_BUILD_TESTS "Build liboeconnect tests" OFF)
option(OEC_BUILD_SHARED "Build shared library" ON)

if(MSVC)
  # /experimental:c11atomics required to enable <stdatomic.h> under /std:c11 on VS 17.5+.
  add_compile_options(/W4 /WX /permissive- /experimental:c11atomics)
else()
  add_compile_options(-Wall -Wextra -Werror -Wpedantic)
endif()

set(OEC_SOURCES
  src/frame.c
  src/ringbuf.c
  src/shm.c
  src/drift.c
  src/sidecar.c
  src/hello.c
)

if(OEC_BUILD_SHARED)
  add_library(oeconnect SHARED ${OEC_SOURCES})
else()
  add_library(oeconnect STATIC ${OEC_SOURCES})
endif()

target_include_directories(oeconnect
  PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
          $<INSTALL_INTERFACE:include>
  PRIVATE src
)

if(UNIX AND NOT APPLE)
  target_link_libraries(oeconnect PRIVATE rt pthread)
endif()
if(APPLE)
  target_link_libraries(oeconnect PRIVATE pthread)
endif()
if(WIN32)
  target_compile_definitions(oeconnect PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set_target_properties(oeconnect PROPERTIES
  C_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  OUTPUT_NAME oeconnect
)

include(GNUInstallDirs)
install(TARGETS oeconnect
  EXPORT oeconnectTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(OEC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
