include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)

add_executable(oec_tests
  test_frame.cc
  test_ringbuf.cc
  test_shm.cc
  test_drift.cc
  test_sidecar.cc
)
target_link_libraries(oec_tests PRIVATE oeconnect GTest::gtest_main)
if(WIN32)
  target_compile_definitions(oec_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
include(GoogleTest)
gtest_discover_tests(oec_tests)

add_executable(oec_stress_ringbuf stress_ringbuf.cc)
target_link_libraries(oec_stress_ringbuf PRIVATE oeconnect)

# Maintenance tool: regenerates the v1.0 golden wire corpus (tests/golden/v1.0).
# Not run by ctest; invoked deliberately via `--target regen_all`.
add_executable(regen_all ${CMAKE_SOURCE_DIR}/../../tests/golden/v1.0/regen_all.cc)
target_link_libraries(regen_all PRIVATE oeconnect)
if(WIN32)
  target_compile_definitions(regen_all PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(WIN32 AND OEC_BUILD_SHARED)
  # Copy liboeconnect runtime next to the test binary so gtest_discover_tests
  # (which runs the binary at build time on Windows) finds the DLL.
  add_custom_command(TARGET oec_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:oeconnect> $<TARGET_FILE_DIR:oec_tests>
  )
  add_custom_command(TARGET oec_stress_ringbuf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:oeconnect> $<TARGET_FILE_DIR:oec_stress_ringbuf>
  )
  add_custom_command(TARGET regen_all POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:oeconnect> $<TARGET_FILE_DIR:regen_all>
  )
endif()
