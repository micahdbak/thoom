add_library(save OBJECT
  save_data.cc
  save_data.h)

target_link_libraries(save
  PUBLIC thoom_headers SDL3::SDL3
  PRIVATE sanitizers)
