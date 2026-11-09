find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

add_executable(test_thoom
  test_thoom.cc)

target_link_libraries(test_thoom
  PRIVATE
    GTest::gtest
    Threads::Threads
    thoom)

add_executable(test_log
  test_log.cc)

target_link_libraries(test_log
  PRIVATE
    GTest::gtest
    Threads::Threads
    absl::log_sink_registry
    thoom)
