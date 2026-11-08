add_library(save OBJECT
  save_data.cc)

target_link_libraries(save
  PUBLIC SDL3::SDL3)
