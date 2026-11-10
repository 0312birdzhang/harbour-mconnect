// Copyright 2021 Sony Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license.
// Shared playback host: phone widgets and first presentation wait for Show.
#include <flutter_elinux.h>
#include <gio/gio.h>
#include <sqlite3.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include "flutter_embedder_options.h"
#include "sailfish_cover.h"

namespace {
constexpr char kName[] = "org.sailfishos.Mconnect";
constexpr char kPath[] = "/org/sailfishos/Mconnect";
constexpr char kInterface[] = "org.sailfishos.Mconnect";
volatile sig_atomic_t stopping = 0;
void Stop(int) { stopping = 1; }
struct Host { bool show = false; bool ui_requested = false; };
void Method(GDBusConnection*, const gchar*, const gchar*, const gchar*,
            const gchar* method, GVariant*, GDBusMethodInvocation* invocation,
            gpointer data) {
  auto* host = static_cast<Host*>(data);
  if (std::string(method) == "Show") host->show = true;
  else if (std::string(method) != "EnsurePlayback") {
    g_dbus_method_invocation_return_dbus_error(invocation,
        "org.sailfishos.Mconnect.UnknownMethod", "Unsupported method");
    return;
  }
  g_dbus_method_invocation_return_value(invocation, nullptr);
}
const GDBusInterfaceVTable vtable = {Method, nullptr, nullptr, {nullptr}};
bool Forward(GDBusConnection* bus, bool background) {
  for (int i = 0; i < 50; ++i) {
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(bus, kName, kPath, kInterface,
        background ? "EnsurePlayback" : "Show", nullptr, nullptr,
        G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, nullptr, &error);
    if (reply) { g_variant_unref(reply); return true; }
    if (error) g_error_free(error);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return false;
}
}

int main(int argc, char** argv) {
  if (!getenv("MCONNECT_SILICA_CORE")) {
    execl("/usr/bin/harbour-mconnect-silica", "harbour-mconnect-silica", static_cast<char*>(nullptr));
    return 1;
  }
  if (sqlite3_initialize() != SQLITE_OK) return 1;
  FlutterEmbedderOptions options;
  if (!options.Parse(argc, argv)) return 1;
  const char* runtime = getenv("XDG_RUNTIME_DIR");
  if (!runtime || runtime[0] != '/') return 1;
  const std::string lock_path = std::string(runtime) + "/mconnect-playback.lock";
  int lock = open(lock_path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (lock < 0) return 1;
  GError* error = nullptr;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (!bus) { if (error) g_error_free(error); close(lock); return 1; }
  if (flock(lock, LOCK_EX | LOCK_NB) != 0) {
    bool ok = Forward(bus, options.IsBackground());
    g_object_unref(bus); close(lock); return ok ? 0 : 1;
  }
  Host host;
  const bool silica_core = getenv("MCONNECT_SILICA_CORE") != nullptr;
  const char* xml = "<node><interface name='org.sailfishos.Mconnect'>"
      "<method name='Show'/><method name='EnsurePlayback'/></interface></node>";
  GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(xml, nullptr);
  guint registration = g_dbus_connection_register_object(bus, kPath,
      info->interfaces[0], &vtable, &host, nullptr, &error);
  if (!registration) { if (error) g_error_free(error); return 1; }
  guint owner = g_bus_own_name_on_connection(bus, kName, G_BUS_NAME_OWNER_FLAGS_NONE,
      nullptr, nullptr, nullptr, nullptr);

  std::string bundle = options.BundlePath();
  std::wstring assets(bundle.begin(), bundle.end()); assets += L"/data/flutter_assets";
  std::wstring icu(bundle.begin(), bundle.end()); icu += L"/data/icudtl.dat";
  std::wstring aot(bundle.begin(), bundle.end()); aot += L"/lib/libapp.so";
  const char* dart_args[] = {"--background"};
  FlutterDesktopEngineProperties properties = {assets.c_str(), icu.c_str(), aot.c_str(),
      options.IsBackground() ? 1 : 0, options.IsBackground() ? dart_args : nullptr};
  FlutterDesktopEngineRef engine = FlutterDesktopEngineCreate(&properties);
  if (!engine) return 1;
  FlutterDesktopViewControllerRef controller = nullptr;
  FlutterDesktopViewProperties view = {};
  view.width = options.WindowWidth(); view.height = options.WindowHeight();
  view.view_mode = options.WindowViewMode() == flutter::FlutterViewController::kFullscreen ? kFullscreen : kNormalscreen;
  view.view_rotation = static_cast<FlutterDesktopViewRotation>(options.WindowRotation());
  const std::string title = options.WindowTitle(), app_id = options.WindowAppID();
  view.title = title.c_str(); view.app_id = app_id.c_str();
  view.use_mouse_cursor = options.IsUseMouseCursor();
  view.use_onscreen_keyboard = options.IsUseOnscreenKeyboard();
  view.use_window_decoration = options.IsUseWindowDecoraation();
  view.text_scale_factor = options.TextScaleFactor();
  view.enable_high_contrast = options.EnableHighContrast();
  view.force_scale_factor = options.IsForceScaleFactor(); view.scale_factor = options.ScaleFactor();
  view.enable_vsync = options.EnableVsync();
  // Allocate the render context before starting Dart. Without a root widget,
  // no buffer is committed and the phone toplevel remains unmapped.
  controller = FlutterDesktopViewControllerCreate(&view, engine);
  if (!controller) return 1;
  engine = FlutterDesktopViewControllerGetEngine(controller);
  if (options.IsBackground())
    fprintf(stderr, "Mconnect host background pid=%d phoneWindow=unmapped\n", getpid());
  else host.ui_requested = true;
  if (!silica_core) StartSailfishCover(bundle.c_str());
  signal(SIGTERM, Stop); signal(SIGINT, Stop);
  int result = 0;
  while (!stopping) {
    PollSailfishCover();
    while (g_main_context_iteration(nullptr, false)) {}
    if (host.show) {
      if (silica_core) {
        g_dbus_connection_call(bus, "org.sailfishos.Mconnect.Silica",
            "/org/sailfishos/Mconnect/Silica", "org.sailfishos.Mconnect.Silica",
            "Show", nullptr, nullptr, G_DBUS_CALL_FLAGS_NO_AUTO_START,
            1500, nullptr, nullptr, nullptr);
        host.show = false;
        continue;
      }
      RaiseSailfishPhone();
      fprintf(stderr, "Mconnect host phone raised pid=%d\n", getpid());
      const bool first_ui = !host.ui_requested;
      host.ui_requested = true;
      fprintf(stderr, "Mconnect host phone view requested pid=%d sharedEngine=1\n", getpid());
      const char lifecycle[] = "AppLifecycleState.resumed";
      FlutterDesktopMessengerSend(FlutterDesktopEngineGetMessenger(engine),
          "flutter/lifecycle", reinterpret_cast<const uint8_t*>(lifecycle), sizeof(lifecycle)-1);
      // Raise creates the Dart root only once for a background-started host.
      // Dart's Raise handler calls Show again; forwarding it every time would
      // create an endless MPRIS Raise -> Show loop.
      if (first_ui) g_dbus_connection_call(bus, "org.mpris.MediaPlayer2.harbour_mconnect",
          "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2", "Raise", nullptr,
          nullptr, G_DBUS_CALL_FLAGS_NO_AUTO_START, 10000, nullptr, nullptr, nullptr);
    }
    host.show = false;
    if (controller && !FlutterDesktopViewDispatchEvent(FlutterDesktopViewControllerGetView(controller))) break;
    // The Qt surface extension owns the compositor close event for these
    // surfaces. Minimise/visibility changes must not be mistaken for close.
    if (SailfishWindowCloseRequested()) {
      fprintf(stderr, "Mconnect host closing pid=%d\n", getpid());
      break;
    }
    FlutterDesktopEngineProcessMessages(engine);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  g_bus_unown_name(owner);
  g_dbus_connection_unregister_object(bus, registration);
  g_dbus_node_info_unref(info); g_object_unref(bus);
  StopSailfishCover();
  if (controller) FlutterDesktopViewControllerDestroy(controller);
  else if (engine) FlutterDesktopEngineDestroy(engine);
  close(lock);
  return result;
}
