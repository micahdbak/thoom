cmake_minimum_required(VERSION 3.21)
project(thoom VERSION 0.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CONFIGURATION_TYPES)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
  endif()
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
      Debug Release RelWithDebInfo MinSizeRel)
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

add_subdirectory(deps/libdatachannel EXCLUDE_FROM_ALL)
add_subdirectory(deps/SDL EXCLUDE_FROM_ALL)

include_directories(
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/include)

if(NOT WIN32)
  add_compile_options(
      $<$<CONFIG:Debug>:-fsanitize=address>
      $<$<CONFIG:Debug>:-fno-omit-frame-pointer>)
  add_link_options(
      $<$<CONFIG:Debug>:-fsanitize=address>)
endif()

add_subdirectory(src)

add_library(thoom
  src/thoom.cc)

target_include_directories(thoom
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(thoom
  PUBLIC
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
    thoom_window
    SDL3::SDL3
  PRIVATE
    LibDataChannel::LibDataChannel)

if(WIN32)
  add_custom_command(TARGET thoom POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:thoom>
          $<TARGET_FILE_DIR:thoom>
      COMMAND_EXPAND_LISTS
  )
endif()
