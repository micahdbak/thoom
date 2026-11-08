add_library(thoom_scene OBJECT
  scene.cc)

target_link_libraries(thoom_scene
  PUBLIC SDL3::SDL3)
