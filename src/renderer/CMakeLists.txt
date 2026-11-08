add_library(renderer OBJECT
  font.cc
  renderer.cc)

target_link_libraries(renderer
  PUBLIC SDL3::SDL3)
