add_library(thoom_texture OBJECT
  texture.cc)

target_link_libraries(thoom_texture
  PUBLIC SDL3::SDL3)
