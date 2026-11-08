add_library(thoom_sprite OBJECT
  sprite.cc)

target_link_libraries(thoom_sprite
  PUBLIC SDL3::SDL3)
