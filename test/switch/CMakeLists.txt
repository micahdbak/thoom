add_executable(test_bytes
  test_bytes.cc)

target_link_libraries(test_bytes
  PRIVATE
    GTest::gtest
    Threads::Threads
    thoom)

add_executable(test_frame
  test_frame.cc)

target_link_libraries(test_frame
  PRIVATE
    GTest::gtest
    Threads::Threads
    thoom)

add_executable(test_switch
  test_switch.cc)

target_link_libraries(test_switch
  PRIVATE
    GTest::gtest
    Threads::Threads
    thoom)
