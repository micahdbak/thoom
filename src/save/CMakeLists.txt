add_library(thoom_save OBJECT
  save_data.cc)

target_link_libraries(thoom_save
  PUBLIC SDL3::SDL3)
