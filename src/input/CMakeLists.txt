add_library(input OBJECT
  controller.cc
  input.cc)

target_link_libraries(input
  PUBLIC SDL3::SDL3)
