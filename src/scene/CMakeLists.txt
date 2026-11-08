add_library(scene OBJECT
  scene.cc
  scene.h)

target_link_libraries(scene
  PUBLIC thoom_headers SDL3::SDL3
  PRIVATE sanitizers)
