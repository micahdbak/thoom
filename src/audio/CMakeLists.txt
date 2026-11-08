add_library(thoom_audio OBJECT
  audio.cc)

target_link_libraries(thoom_audio
  PUBLIC SDL3::SDL3)
