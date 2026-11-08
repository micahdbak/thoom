add_library(utils OBJECT
  utils.cc
  utils.h)

target_link_libraries(utils
  PUBLIC thoom_headers SDL3::SDL3
  PRIVATE sanitizers)
