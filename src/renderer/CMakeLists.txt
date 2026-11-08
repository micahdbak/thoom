add_library(thoom_renderer OBJECT
  font.cc
  renderer.cc)

target_link_libraries(thoom_renderer
  PUBLIC SDL3::SDL3)
