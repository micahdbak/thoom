add_library(texture OBJECT
  texture.cc)

target_link_libraries(texture
  PUBLIC SDL3::SDL3)
