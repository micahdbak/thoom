add_library(texture OBJECT
  texture.cc
  texture.h)

target_link_libraries(texture
  PUBLIC thoom_headers SDL3::SDL3
  PRIVATE sanitizers)
