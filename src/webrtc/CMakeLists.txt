add_library(thoom_webrtc OBJECT
  webrtc.cc
  webrtc_connection.cc
  webrtc_signaller.cc)

target_link_libraries(thoom_webrtc
  PRIVATE LibDataChannel::LibDataChannel)
