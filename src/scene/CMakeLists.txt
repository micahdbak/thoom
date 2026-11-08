add_library(scene OBJECT
  scene.cc)

target_link_libraries(scene
  PUBLIC SDL3::SDL3)
