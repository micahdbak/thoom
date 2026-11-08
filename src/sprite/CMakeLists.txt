add_library(sprite OBJECT
  sprite.cc
  sprite.h)

target_link_libraries(sprite
  PUBLIC thoom_headers SDL3::SDL3
  PRIVATE sanitizers)
