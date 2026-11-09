add_executable(test_peer
  test_peer.cc)

target_link_libraries(test_peer
  PRIVATE
    GTest::gtest
    Threads::Threads
    thoom)
