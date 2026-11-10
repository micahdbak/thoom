add_library(thoom_comms OBJECT
  bytes.cc
  comms.cc
  messages.cc
  protocol.cc
  session.cc)

target_link_libraries(thoom_comms
  PRIVATE thoom_webrtc)
