add_library(map OBJECT
  map.cc
  map_collider.cc
  map_layer.cc
  tilesheet.cc)

target_link_libraries(map
  PUBLIC SDL3::SDL3)
