add_library(thoom_hud OBJECT
  hud.cc)

target_link_libraries(thoom_hud
  PUBLIC SDL3::SDL3)
