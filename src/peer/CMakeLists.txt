add_library(thoom_peer OBJECT
  message_queue.cc
  peer.cc
  peer_connection.cc
  peer_signaller.cc
  peer_thread.cc)

target_link_libraries(thoom_peer
  PRIVATE LibDataChannel::LibDataChannel)
