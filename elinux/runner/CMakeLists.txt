cmake_minimum_required(VERSION 3.15)
project(runner LANGUAGES CXX)

if(FLUTTER_TARGET_BACKEND_TYPE MATCHES "gbm")
  add_definitions(-DFLUTTER_TARGET_BACKEND_GBM)
elseif(FLUTTER_TARGET_BACKEND_TYPE MATCHES "eglstream")
  add_definitions(-DFLUTTER_TARGET_BACKEND_EGLSTREAM)
elseif(FLUTTER_TARGET_BACKEND_TYPE MATCHES "x11")
  add_definitions(-DFLUTTER_TARGET_BACKEND_X11)
else()
  add_definitions(-DFLUTTER_TARGET_BACKEND_WAYLAND)
endif()

add_executable(${BINARY_NAME}
  "flutter_window.cc"
  "main.cc"
  "sailfish_cover.cc"
  "sailfish_cover.h"
  "${FLUTTER_MANAGED_DIR}/generated_plugin_registrant.cc"
)
apply_standard_settings(${BINARY_NAME})
target_link_libraries(${BINARY_NAME} PRIVATE flutter)
target_link_libraries(${BINARY_NAME} PRIVATE flutter flutter_wrapper_app)
target_include_directories(${BINARY_NAME} PRIVATE "${CMAKE_SOURCE_DIR}")
add_dependencies(${BINARY_NAME} flutter_assemble)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
target_link_libraries(${BINARY_NAME} PRIVATE PkgConfig::GIO)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)
target_link_libraries(${BINARY_NAME} PRIVATE PkgConfig::SQLITE)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)
add_library(mconnect_audio SHARED sailfish_audio.cc)
apply_standard_settings(mconnect_audio)
target_link_libraries(mconnect_audio PRIVATE PkgConfig::GSTREAMER)

set_target_properties(${BINARY_NAME} PROPERTIES AUTOMOC ON ENABLE_EXPORTS ON)
find_package(Qt5 REQUIRED COMPONENTS Core Gui DBus Network WaylandClient)
target_link_libraries(${BINARY_NAME} PRIVATE Qt5::Gui Qt5::Network Qt5::DBus Qt5::WaylandClient dl wayland-client "-l:libwayland-egl.so.1")
target_include_directories(${BINARY_NAME} PRIVATE ${Qt5WaylandClient_PRIVATE_INCLUDE_DIRS})

option(MCONNECT_BUILD_COVER_TESTS "Build isolated native cover tests" OFF)
if(MCONNECT_BUILD_COVER_TESTS)
  add_executable(mconnect_cover_test
    ../../test/native_cover_test.cc sailfish_cover.cc sailfish_cover.h)
  set_target_properties(mconnect_cover_test PROPERTIES AUTOMOC ON)
  target_include_directories(mconnect_cover_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${Qt5WaylandClient_PRIVATE_INCLUDE_DIRS})
  target_compile_options(mconnect_cover_test PRIVATE -UNDEBUG)
  target_link_libraries(mconnect_cover_test PRIVATE Qt5::Gui Qt5::Network
    Qt5::DBus Qt5::WaylandClient dl wayland-client "-l:libwayland-egl.so.1")
  add_executable(mconnect_window_lifecycle_test
    ../../test/native_window_lifecycle_test.cc sailfish_cover.h)
  set_target_properties(mconnect_window_lifecycle_test PROPERTIES AUTOMOC ON)
  target_include_directories(mconnect_window_lifecycle_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${Qt5WaylandClient_PRIVATE_INCLUDE_DIRS})
  target_compile_options(mconnect_window_lifecycle_test PRIVATE -UNDEBUG)
  target_link_libraries(mconnect_window_lifecycle_test PRIVATE Qt5::Gui
    Qt5::Network Qt5::DBus Qt5::WaylandClient dl wayland-client wayland-server
    "-l:libwayland-egl.so.1")
endif()
