add_library(input OBJECT
  controller.cc
  controller.h
  input.cc
  input.h)

target_link_libraries(input
  PUBLIC thoom_headers SDL3::SDL3
  PRIVATE sanitizers)
