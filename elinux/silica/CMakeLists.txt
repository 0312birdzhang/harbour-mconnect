add_library(mconnect_native_core STATIC
  ../third_party/qrcodegen/qrcodegen.cpp
  native_account.cpp
  native_activation.cpp
  native_artwork.cpp
  native_bilibili.cpp
  native_catalog.cpp
  native_car_content.cpp
  native_content.cpp
  native_core.cpp
  native_database.cpp
  native_downloads.cpp
  native_hive.cpp
  native_kugou.cpp
  native_kugou_account.cpp
  native_kugou_content.cpp
  native_kugou_library.cpp
  native_library.cpp
  native_local_metadata.cpp
  native_local_music.cpp
  native_login.cpp
  native_lyric_document.cpp
  native_lyric_loader.cpp
  native_lyrics.cpp
  native_mpris.cpp
  native_music.cpp
  native_platforms.cpp
  native_platforms.h
  native_presentation.cpp
  native_player.cpp
  native_qq.cpp
  native_qq_account.cpp
  native_runtime.cpp
  native_sessions.cpp
  native_source_match.cpp
  native_storage.cpp
  platform_http.cpp
  platform_models.cpp)
apply_standard_settings(mconnect_native_core)
target_compile_options(mconnect_native_core PRIVATE -Wno-misleading-indentation)
target_link_libraries(mconnect_native_core PUBLIC Qt5::Gui Qt5::DBus Qt5::Network
  Qt5::Quick PkgConfig::SQLITE PkgConfig::TAGLIB ZLIB::ZLIB)

set(MCONNECT_SAILPLAY_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sailplay-sdk/sdk/qt" CACHE PATH "Sailplay Qt SDK source")
add_subdirectory("${MCONNECT_SAILPLAY_SDK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/sailplay-sdk")
target_link_libraries(mconnect_native_core PUBLIC sailplay_sdk_provider)
install(FILES "${MCONNECT_SAILPLAY_SDK_DIR}/../../LICENSE" DESTINATION share/licenses/harbour-mconnect RENAME sailplay-sdk-LICENSE)

add_library(mconnect_native_ui STATIC bridge.cpp bridge.h)
apply_standard_settings(mconnect_native_ui)
target_link_libraries(mconnect_native_ui PUBLIC mconnect_native_core)

add_executable(mconnect-silica main.cpp)
set_target_properties(mconnect-silica PROPERTIES
  OUTPUT_NAME harbour-mconnect-silica
  INSTALL_RPATH "$ORIGIN/../libexec/harbour-mconnect/lib")
apply_standard_settings(mconnect-silica)
target_compile_definitions(mconnect-silica PRIVATE MCONNECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(mconnect-silica PRIVATE mconnect_native_ui mconnect_native_core mconnect_audio sailplay_sdk_provider
  Qt5::Quick Qt5::Qml PkgConfig::SAILFISHAPP)

if(MCONNECT_BUILD_TESTS OR MCONNECT_BUILD_COVER_TESTS)
  add_library(mconnect_fixture_audio STATIC ../../test/sailfish_audio_fixture.cc)
  target_link_libraries(mconnect_fixture_audio PUBLIC Qt5::Core)
  apply_standard_settings(mconnect_fixture_audio)
  foreach(module qq_account qq kugou kugou_content kugou_account kugou_library
      bilibili account catalog content platforms storage music library core_service
      downloads login local_music lyric_document lyric_loader player runtime source_match mpris
      ui artwork lyrics lyrics_bridge core_transfer presentation lifecycle car_content)
    add_executable(mconnect_native_${module}_test ../../test/native_${module}_test.cc)
    apply_standard_settings(mconnect_native_${module}_test)
    target_compile_options(mconnect_native_${module}_test PRIVATE -Wno-misleading-indentation)
    target_link_libraries(mconnect_native_${module}_test PRIVATE mconnect_native_ui mconnect_native_core mconnect_fixture_audio sailplay_sdk_provider)
    add_test(NAME native_${module} COMMAND mconnect_native_${module}_test)
  endforeach()
endif()
