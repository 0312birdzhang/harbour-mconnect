import 'dart:async';
import 'dart:convert';
import 'dart:ui' as ui;

import 'package:qr_flutter/qr_flutter.dart';

import '../features/auth/presentation/providers/auth_provider.dart';
import '../models/platform_type.dart';
import '../platform/base/music_platform.dart';

/// Owns one bounded QR attempt; keys and cookies stay in the existing auth layer.
class QmlLoginController {
  QmlLoginController({
    required this.auth,
    required this.publish,
    this.render = renderQr,
    this.lifetime = const Duration(minutes: 3),
  });
  final AuthNotifier auth;
  final Future<void> Function(String topic, Object? value) publish;
  final Future<String> Function(QrLoginResult qr) render;
  final Duration lifetime;
  StreamSubscription<QrLoginStatus>? _poll;
  Timer? _expiry;
  int _generation = 0;
  Map<String, Object?> _state = {};
  Map<String, Object?> get state => Map.of(_state);

  static Future<String> renderQr(QrLoginResult qr) async {
    final bytes = qr.qrBytes;
    if (bytes != null && bytes.isNotEmpty) {
      if (bytes.length > 1024 * 1024) throw StateError('QR image too large');
      return 'data:image/png;base64,${base64Encode(bytes)}';
    }
    final url = qr.qrUrl;
    if (url == null || url.isEmpty) throw StateError('Missing QR payload');
    final painter = QrPainter(
      data: url,
      version: QrVersions.auto,
      gapless: true,
      eyeStyle: const QrEyeStyle(
        eyeShape: QrEyeShape.square,
        color: ui.Color(0xff000000),
      ),
      dataModuleStyle: const QrDataModuleStyle(
        dataModuleShape: QrDataModuleShape.square,
        color: ui.Color(0xff000000),
      ),
    );
    final data = await painter.toImageData(512, format: ui.ImageByteFormat.png);
    if (data == null) throw StateError('QR image unavailable');
    return 'data:image/png;base64,${base64Encode(data.buffer.asUint8List())}';
  }

  Future<Map<String, Object?>> begin(
    PlatformType platform, {
    String? variant,
    String? session,
  }) async {
    if (variant != null &&
        (platform != PlatformType.kugou ||
            !const ['lite', 'android'].contains(variant))) {
      throw ArgumentError();
    }
    final clientSession = session ?? '${_generation + 1}';
    if (clientSession.isEmpty || clientSession.length > 128) {
      throw ArgumentError();
    }
    final generation = ++_generation;
    _expiry?.cancel();
    final previous = _poll;
    _poll = null;
    _state = {
      'session': clientSession,
      'platform': platform.name,
      'status': 'loading',
      'image': '',
    };
    await previous?.cancel();
    if (generation != _generation) return {'cancelled': true};
    try {
      final qr = await auth
          .getQrCodeWithVariant(platform, authVariant: variant)
          .timeout(const Duration(seconds: 15));
      final image = await render(qr).timeout(const Duration(seconds: 3));
      if (generation != _generation) return {'cancelled': true};
      _state = {
        'session': clientSession,
        'platform': platform.name,
        'status': 'waiting',
        'image': image,
      };
      _expiry = Timer(lifetime, () {
        if (generation == _generation) {
          unawaited(_poll?.cancel());
          _setStatus('expired');
        }
      });
      _poll = auth
          .pollQrStatus(platform, qr.key, authVariant: variant)
          .listen(
            (status) {
              unawaited(_status(generation, platform, status));
            },
            onError: (Object _) {
              if (generation == _generation) {
                _expiry?.cancel();
                _setStatus('failed');
              }
            },
            onDone: () {
              if (generation == _generation &&
                  const ['waiting', 'scanned'].contains(_state['status'])) {
                _setStatus('expired');
              }
            },
          );
      return state;
    } catch (_) {
      if (generation != _generation) return {'cancelled': true};
      _setStatus('failed');
      rethrow;
    }
  }

  Future<void> _status(
    int generation,
    PlatformType platform,
    QrLoginStatus value,
  ) async {
    if (generation != _generation) return;
    if (value == QrLoginStatus.success) {
      _setStatus('saving');
      _expiry?.cancel();
      try {
        final user = await auth.onQrLoginSuccess(platform);
        if (user == null) {
          if (generation == _generation) _setStatus('failed');
          return;
        }
      } catch (_) {
        if (generation == _generation) _setStatus('failed');
        return;
      }
      if (generation != _generation) return;
      _setStatus('success');
    } else {
      _setStatus(value.name);
      if (value == QrLoginStatus.expired || value == QrLoginStatus.failed) {
        _expiry?.cancel();
      }
    }
  }

  void _setStatus(String status) {
    _state = {..._state, 'status': status};
    // The image is returned once; status notifications are small.
    unawaited(
      publish('login', {
        for (final e in _state.entries)
          if (e.key != 'image') e.key: e.value,
      }),
    );
  }

  Future<void> cancel(String? session) async {
    if (session != null && _state['session'] != session) return;
    ++_generation;
    _expiry?.cancel();
    final previous = _poll;
    _poll = null;
    _state = {};
    await previous?.cancel();
  }
}
