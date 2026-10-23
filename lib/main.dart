import 'dart:async';
import 'package:dbus/dbus.dart';
import 'sailfish/mpris.dart';
import 'features/auth/presentation/providers/auth_provider.dart';
import 'features/player/presentation/providers/player_provider.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:hive_flutter/hive_flutter.dart';
import 'package:liquid_glass_widgets/liquid_glass_widgets.dart';
import 'app.dart';
import 'sailfish/paths.dart';
import 'sailfish/secret_storage.dart';
import 'package:flutter_secure_storage_platform_interface/flutter_secure_storage_platform_interface.dart';
import 'package:path_provider_platform_interface/path_provider_platform_interface.dart';
import 'core/constants/app_constants.dart';
import 'core/diagnostics/crash_reporter.dart';
import 'core/diagnostics/diagnostics_service.dart';
import 'features/player/data/background_audio_initializer.dart';
import 'features/widget/widget_bridge.dart';
import 'features/widget/widget_bridge_observer.dart';
import 'platform/base/platform_registry.dart';
import 'platform/netease/netease_platform.dart';
import 'platform/qq/qq_platform.dart';
import 'platform/kugou/kugou_platform.dart';
import 'platform/bilibili/bilibili_platform.dart';

ProviderContainer? _playbackContainer;
final Completer<void> _coreReady = Completer<void>();
bool _phoneUiCreated = false;

void _showPhoneUi() {
  if (_phoneUiCreated) return;
  _phoneUiCreated = true;
  DiagnosticsService.instance.startUiHeartbeat();
  runApp(
    UncontrolledProviderScope(
      container: _playbackContainer!,
      child: const MconnectApp(),
    ),
  );
}

void main(List<String> args) => _startMconnect(background: args.contains('--background'));

Future<void> _startMconnect({required bool background}) async {
  WidgetsFlutterBinding.ensureInitialized();
  SailfishMpris.instance.onRaise = () async {
    await _coreReady.future;
    final bus = DBusClient.session();
    try {
      final host = DBusRemoteObject(
        bus,
        name: 'org.sailfishos.Mconnect',
        path: DBusObjectPath('/org/sailfishos/Mconnect'),
      );
      await host.callMethod('org.sailfishos.Mconnect', 'Show', []);
      _showPhoneUi();
    } finally {
      await bus.close();
    }
  };
  FlutterSecureStoragePlatform.instance = SailfishSecureStoragePlatform();
  PathProviderPlatform.instance = SailfishPathProvider();

  // Apply the documented image cache budget. Until now nothing read this
  // constant, so the app ran on the framework default (100 MB) while the source
  // claimed 200 MB - a "tunable" that changed nothing.
  PaintingBinding.instance.imageCache.maximumSizeBytes =
      AppConstants.imageCacheSizeMb * 1024 * 1024;

  final diagnostics = DiagnosticsService.instance;

  // These three jobs are independent, so they run concurrently instead of
  // paying their latencies one after another before the first frame. The only
  // ordering that matters is Hive.initFlutter() before openBox('settings'),
  // which is why they are wrapped together.
  await Future.wait<void>([
    if (!background) _prewarmGlassWidgets(),
    diagnostics.initialize(),
    _openSettingsBox(),
  ]);
  if (!background) diagnostics.startUiHeartbeat();

  // Audio/session setup must finish before playback can be requested, but it
  // does not depend on the three jobs above (it is handed the already
  // initialised diagnostics instance).
  await BackgroundAudioInitializer.initialize(diagnostics: diagnostics);

  // Crash reporting: local by default (the existing diagnostics log, redacted),
  // and remote **only** when a DSN was supplied at build time
  // (`--dart-define=MCONNECT_SENTRY_DSN=…`) — see `CrashReporter`. With no DSN
  // there is no network behaviour at all, which is a structural guarantee rather
  // than a promise to remember.
  final crashReporter = CrashReporter(diagnostics: diagnostics);

  // The three process-wide entries. `installCrashHandling` takes over
  // `FlutterError.onError` and `ErrorWidget.builder` and **returns** the zone
  // callback `runZonedGuarded` needs, so the wiring stays testable.
  final onZoneError = installCrashHandling(
    crashReporter,
    // Unchanged red-screen text: this only adds "report it on the way out".
    buildErrorWidget: (details) {
      return Material(
        child: Center(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Text(
              '发生了错误\n${details.exceptionAsString()}',
              textAlign: TextAlign.center,
              style: const TextStyle(color: Colors.red),
            ),
          ),
        ),
      );
    },
    // Same console output as the inline handler had: presentError + debugPrint.
    presentError: (details) {
      FlutterError.presentError(details);
      debugPrint('FlutterError: ${details.exceptionAsString()}');
    },
  );

  // Register platforms
  PlatformRegistry.register(NeteasePlatform());
  PlatformRegistry.register(QqPlatform());
  PlatformRegistry.register(KugouPlatform());
  PlatformRegistry.register(BilibiliPlatform());

  // W3-A: home-screen widget bridge (see lib/features/widget/).
  //
  // `home_widget`'s pubspec declares `platforms:` for **android and ios only**, so
  // on Windows the plugin is simply not built or registered: nothing fails to
  // compile, but ANY `HomeWidget.*` call would throw MissingPluginException.
  // `WidgetBridge` therefore checks `isSupported` before touching the plugin and is
  // a hard no-op elsewhere (test/widget_bridge_test.dart pins that with a plugin-call
  // counter). Every failure here is swallowed and reported: the widget is a
  // nice-to-have and must never be able to break startup.
  await WidgetBridge.initialize(
    onError: (message, error, stack) =>
        diagnostics.recordError(message, error, stack),
  );

  await runZonedGuarded(
    () async {
      _playbackContainer = ProviderContainer(
        observers: [WidgetBridgeObserver()],
      );
      await _playbackContainer!.read(authProvider.notifier).init();
      // The shared notifier restores the queue and registers MPRIS without a UI.
      _playbackContainer!.read(playerProvider);
      _coreReady.complete();
      if (!background) _showPhoneUi();
      debugPrint('Mconnect playback core ready; background=$background');
    },
    (error, stack) {
      if (!_coreReady.isCompleted) _coreReady.completeError(error, stack);
      debugPrint('Uncaught error: $error\n$stack');
      onZoneError(error, stack);
    },
  );
}

/// Pre-warms the liquid-glass fragment shader (a disk read).
///
/// Deliberately failure-tolerant: the platform may not support it and the
/// shader asset may be missing, in which case version 1.7.2 of the package
/// falls back to `BackdropFilter` or an opaque fill on its own. Startup must
/// never be blocked by a pre-warm, which is why the error is only logged.
Future<void> _prewarmGlassWidgets() async {
  try {
    await LiquidGlassWidgets.initialize();
  } catch (error, stack) {
    debugPrint('LiquidGlassWidgets.initialize 失败（已忽略）: $error\n$stack');
  }
}

/// Opens the Hive settings box. `initFlutter` must run first - those two are the
/// only ordered steps in the pre-runApp block.
Future<void> _openSettingsBox() async {
  Hive.init((await getApplicationSupportDirectory()).path);
  await Hive.openBox('settings');
}
