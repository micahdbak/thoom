add_library(hud OBJECT
  hud.cc
  hud.h)

target_link_libraries(hud
  PUBLIC thoom_headers SDL3::SDL3
  PRIVATE sanitizers)
