add_library(map OBJECT
  map.cc
  map.h
  map_collider.cc
  map_collider.h
  map_layer.cc
  map_layer.h
  tilesheet.cc
  tilesheet.h)

target_link_libraries(map
  PUBLIC thoom_headers SDL3::SDL3
  PRIVATE sanitizers)
