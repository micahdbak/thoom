add_executable(thoom_map_editor
  main.cc
  editor.cc
  editor_ui.cc)
add_executable(thoom::map_editor ALIAS thoom_map_editor)

set_target_properties(thoom_map_editor PROPERTIES
  EXPORT_NAME map_editor
  OUTPUT_NAME map_editor)

target_include_directories(thoom_map_editor
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(thoom_map_editor
  PRIVATE cxx_std_20)

target_link_libraries(thoom_map_editor
  PRIVATE thoom::thoom)

if(WIN32)
  add_custom_command(TARGET thoom_map_editor POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:thoom_map_editor>
          $<TARGET_FILE_DIR:thoom_map_editor>
      COMMAND_EXPAND_LISTS
  )
endif()
