add_library(hud OBJECT
  hud.cc)

target_link_libraries(hud
  PUBLIC SDL3::SDL3)
