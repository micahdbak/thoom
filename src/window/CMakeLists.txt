add_library(window OBJECT
  window.cc)

target_link_libraries(window
  PUBLIC SDL3::SDL3)
