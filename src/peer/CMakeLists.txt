add_library(thoom_peer OBJECT
  peer.cc
  peer_connection.cc
  peer_signaller.cc)

target_link_libraries(thoom_peer
  PRIVATE LibDataChannel::LibDataChannel)
