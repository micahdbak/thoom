add_library(window OBJECT
  window.cc
  window.h)

target_link_libraries(window
  PUBLIC thoom_headers SDL3::SDL3
  PRIVATE sanitizers)
