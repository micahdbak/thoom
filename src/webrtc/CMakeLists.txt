add_library(thoom_webrtc OBJECT
  webrtc_signaller.cc
  webrtc_switch.cc)

target_link_libraries(thoom_webrtc
  PRIVATE LibDataChannel::LibDataChannel)
