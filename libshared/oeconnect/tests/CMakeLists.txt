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
include(GoogleTest)
gtest_discover_tests(oec_tests)

add_executable(oec_stress_ringbuf stress_ringbuf.cc)
target_link_libraries(oec_stress_ringbuf PRIVATE oeconnect)
