add_library(thoom_input OBJECT
  controller.cc
  input.cc)

target_link_libraries(thoom_input
  PUBLIC SDL3::SDL3)
