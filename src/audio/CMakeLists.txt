add_library(audio OBJECT
  audio.cc
  audio.h)

target_link_libraries(audio
  PUBLIC thoom_headers SDL3::SDL3
  PRIVATE sanitizers)
