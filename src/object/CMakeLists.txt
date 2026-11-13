add_library(thoom_object OBJECT
  area.cc
  entity.cc
  object.cc)

target_link_libraries(thoom_object
  PUBLIC SDL3::SDL3)
