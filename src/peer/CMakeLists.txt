add_library(peer OBJECT
  peer.cc
  peer.h
  peer_connection.cc
  peer_connection.h
  peer_signaller.cc
  peer_signaller.h)

target_link_libraries(peer
  PUBLIC thoom_headers SDL3::SDL3 LibDataChannel::LibDataChannel
  PRIVATE sanitizers)
