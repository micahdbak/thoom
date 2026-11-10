add_executable(test_scene
  test_scene.cc)

target_link_libraries(test_scene
  PRIVATE
    GTest::gtest
    Threads::Threads
    thoom)
