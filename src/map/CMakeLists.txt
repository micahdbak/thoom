add_library(thoom_map OBJECT
  map.cc
  map_collider.cc
  map_layer.cc
  tilesheet.cc)

target_link_libraries(thoom_map
  PUBLIC SDL3::SDL3)
