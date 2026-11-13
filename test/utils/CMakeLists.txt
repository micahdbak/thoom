add_executable(test_bytes
  test_bytes.cc)

target_link_libraries(test_bytes
  PRIVATE
    GTest::gtest
    Threads::Threads
    thoom)
