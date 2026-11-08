add_library(peer OBJECT
  peer.cc
  peer_connection.cc
  peer_signaller.cc)

target_link_libraries(peer
  PRIVATE LibDataChannel::LibDataChannel)
