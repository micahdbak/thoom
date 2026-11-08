add_library(sprite OBJECT
  sprite.cc)

target_link_libraries(sprite
  PUBLIC SDL3::SDL3)
