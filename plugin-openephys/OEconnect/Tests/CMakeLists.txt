include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(oec_plugin_tests
  test_ack_outbox.cc
  test_shmem_transport.cc
  test_zmq_transport.cc
  test_processor_hot_path.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../Source/Transport/ShmemTransport.cpp
)
target_link_libraries(oec_plugin_tests
  PRIVATE oeconnect libzmq-static cppzmq-static GTest::gtest_main
)
target_include_directories(oec_plugin_tests
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source
)
if(WIN32 AND TARGET oeconnect)
  # Copy liboeconnect runtime next to the test exe so gtest_discover_tests
  # (which runs the binary post-build on Windows) finds the DLL.
  add_custom_command(TARGET oec_plugin_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:oeconnect> $<TARGET_FILE_DIR:oec_plugin_tests>
  )
endif()

include(GoogleTest)
gtest_discover_tests(oec_plugin_tests)
