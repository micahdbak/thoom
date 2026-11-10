add_executable(thoom_webrtc_bench
  bench.cc
  webrtc_bench.cc)

add_executable(thoom_comms_bench
  bench.cc
  comms_bench.cc
  comms_session_bench.cc
  loopback.cc)

set_target_properties(thoom_webrtc_bench PROPERTIES
  OUTPUT_NAME webrtc_bench)

set_target_properties(thoom_comms_bench PROPERTIES
  OUTPUT_NAME comms_bench)

foreach(bench IN ITEMS thoom_webrtc_bench thoom_comms_bench)
  target_compile_features(${bench} PRIVATE cxx_std_20)
  target_link_libraries(${bench} PRIVATE absl::log_initialize thoom::thoom)

  if(WIN32)
    add_custom_command(TARGET ${bench} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${bench}>
            $<TARGET_FILE_DIR:${bench}>
        COMMAND_EXPAND_LISTS
    )
  endif()
endforeach()
