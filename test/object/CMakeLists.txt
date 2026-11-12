add_executable(test_object
  test_object.cc)

target_link_libraries(test_object
  PRIVATE
    GTest::gtest
    Threads::Threads
    thoom)
