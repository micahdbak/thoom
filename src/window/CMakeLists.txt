add_library(thoom_window OBJECT
  window.cc)

target_link_libraries(thoom_window
  PUBLIC SDL3::SDL3)
