add_library(renderer OBJECT
  font.cc
  font.h
  renderer.cc
  renderer.h)

target_link_libraries(renderer
  PUBLIC thoom_headers SDL3::SDL3
  PRIVATE sanitizers)
