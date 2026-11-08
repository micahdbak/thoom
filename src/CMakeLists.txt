add_library(thoom_headers INTERFACE)

target_include_directories(thoom_headers
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include)

add_subdirectory(audio)
add_subdirectory(hud)
add_subdirectory(input)
add_subdirectory(map)
add_subdirectory(peer)
add_subdirectory(renderer)
add_subdirectory(save)
add_subdirectory(scene)
add_subdirectory(sprite)
add_subdirectory(texture)
add_subdirectory(utils)
add_subdirectory(window)

add_library(thoom
  thoom.cc)

target_link_libraries(thoom
  PUBLIC
    thoom_headers
    audio
    hud
    input
    map
    peer
    renderer
    save
    scene
    sprite
    texture
    utils
    window
    LibDataChannel::LibDataChannel
    SDL3::SDL3
  PRIVATE sanitizers)

if(WIN32)
  add_custom_command(TARGET thoom POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:thoom>
          $<TARGET_FILE_DIR:thoom>
      COMMAND_EXPAND_LISTS
  )
endif()
