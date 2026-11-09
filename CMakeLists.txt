cmake_minimum_required(VERSION 3.21)
project(thoom VERSION 0.1 LANGUAGES C CXX)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  if(NOT CMAKE_CONFIGURATION_TYPES)
    if(NOT CMAKE_BUILD_TYPE)
      set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    endif()
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
  endif()
endif()

if(WIN32)
  set(BUILD_SHARED_LIBS ON)
  set(SDL3_DISABLE_WINDOWS_GAMEINPUT TRUE CACHE BOOL "" FORCE)
else()
  set(BUILD_SHARED_LIBS OFF)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(PkgConfig REQUIRED)
  find_package(X11 REQUIRED)
  pkg_check_modules(ALSA REQUIRED alsa)
  pkg_check_modules(DBUS REQUIRED dbus-1)
  pkg_check_modules(IBUS REQUIRED ibus-1.0)
  pkg_check_modules(LIBDECOR REQUIRED libdecor-0)
  pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
  pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3>=0.3.44)
  pkg_check_modules(PULSEAUDIO REQUIRED libpulse)
  pkg_check_modules(WAYLAND REQUIRED wayland-client)
  pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
endif()

set(THOOM_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT WIN32)
  add_library(thoom_sanitizers INTERFACE)
  target_compile_options(thoom_sanitizers INTERFACE
      $<$<CONFIG:Debug>:-fsanitize=address>
      $<$<CONFIG:Debug>:-fno-omit-frame-pointer>)
  target_link_options(thoom_sanitizers INTERFACE
      $<$<CONFIG:Debug>:-fsanitize=address>)
endif()

add_subdirectory(deps/libdatachannel EXCLUDE_FROM_ALL)
add_subdirectory(deps/SDL EXCLUDE_FROM_ALL)

set(THOOM_ENGINE_LIBS
  thoom_audio
  thoom_hud
  thoom_input
  thoom_map
  thoom_peer
  thoom_renderer
  thoom_save
  thoom_scene
  thoom_sprite
  thoom_texture
  thoom_utils
  thoom_webrtc
  thoom_window)

add_subdirectory(src)

foreach(lib IN LISTS THOOM_ENGINE_LIBS)
  target_compile_features(${lib} PRIVATE cxx_std_20)
  target_include_directories(${lib} PRIVATE
    ${THOOM_SOURCE_DIR}/include
    ${THOOM_SOURCE_DIR}/src)
  if(NOT WIN32)
    target_link_libraries(${lib} PRIVATE thoom_sanitizers)
  endif()
endforeach()

add_library(thoom
  src/thoom.cc)
add_library(thoom::thoom ALIAS thoom)

target_compile_features(thoom
  PUBLIC cxx_std_20)

target_include_directories(thoom
  PUBLIC
    $<BUILD_INTERFACE:${THOOM_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${THOOM_SOURCE_DIR}/src>)

target_link_libraries(thoom
  PUBLIC
    ${THOOM_ENGINE_LIBS}
    SDL3::SDL3
  PRIVATE
    LibDataChannel::LibDataChannel)

if(NOT WIN32)
  target_link_libraries(thoom
    PUBLIC thoom_sanitizers)
endif()

if(WIN32)
  add_custom_command(TARGET thoom POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:thoom>
          $<TARGET_FILE_DIR:thoom>
      COMMAND_EXPAND_LISTS
  )
endif()

if(PROJECT_IS_TOP_LEVEL)
  add_subdirectory(tools)
else()
  add_subdirectory(tools EXCLUDE_FROM_ALL)
endif()
