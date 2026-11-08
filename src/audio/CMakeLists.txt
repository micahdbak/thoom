add_library(audio OBJECT
  audio.cc)

target_link_libraries(audio
  PUBLIC SDL3::SDL3)
