import '../l10n/l10n.dart';
import 'dart:async';
import 'dart:ffi';
import 'package:ffi/ffi.dart';
import 'package:just_audio/just_audio.dart' show ProcessingState;
import '../features/player/data/player_audio_controller.dart';

/// Uses the native GStreamer playbin bridge included in the application RPM.
class SailfishAudioController
    implements PlayerAudioController, PlaybackSpeedCapable {
  SailfishAudioController({DynamicLibrary? library}) {
    final lib = library ?? DynamicLibrary.open('libmconnect_audio.so');
    final create = lib
        .lookupFunction<Pointer<Void> Function(), Pointer<Void> Function()>(
          'mc_audio_create',
        );
    _handle = create();
    if (_handle == nullptr) {
      throw StateError(zhAppLocalizations.sfosPlaybinUnavailable);
    }
    _destroy = lib
        .lookupFunction<
          Void Function(Pointer<Void>),
          void Function(Pointer<Void>)
        >('mc_audio_destroy');
    _open = lib
        .lookupFunction<
          Int32 Function(Pointer<Void>, Pointer<Utf8>),
          int Function(Pointer<Void>, Pointer<Utf8>)
        >('mc_audio_open');
    _setState = lib
        .lookupFunction<
          Int32 Function(Pointer<Void>, Int32),
          int Function(Pointer<Void>, int)
        >('mc_audio_state');
    _seek = lib
        .lookupFunction<
          Int32 Function(Pointer<Void>, Int64),
          int Function(Pointer<Void>, int)
        >('mc_audio_seek');
    _position = lib
        .lookupFunction<
          Int64 Function(Pointer<Void>),
          int Function(Pointer<Void>)
        >('mc_audio_position');
    _duration = lib
        .lookupFunction<
          Int64 Function(Pointer<Void>),
          int Function(Pointer<Void>)
        >('mc_audio_duration');
    _status = lib
        .lookupFunction<
          Int32 Function(Pointer<Void>),
          int Function(Pointer<Void>)
        >('mc_audio_status');
    _error = lib
        .lookupFunction<
          Pointer<Utf8> Function(Pointer<Void>),
          Pointer<Utf8> Function(Pointer<Void>)
        >('mc_audio_error');
    _volumeFn = lib
        .lookupFunction<
          Void Function(Pointer<Void>, Double),
          void Function(Pointer<Void>, double)
        >('mc_audio_volume');
    _rateFn = lib
        .lookupFunction<
          Int32 Function(Pointer<Void>, Double),
          int Function(Pointer<Void>, double)
        >('mc_audio_rate');
    _equalizer = lib
        .lookupFunction<
          Int32 Function(Pointer<Void>, Pointer<Double>),
          int Function(Pointer<Void>, Pointer<Double>)
        >('mc_audio_equalizer');
    _timer = Timer.periodic(const Duration(milliseconds: 200), (_) => _poll());
  }

  late final Pointer<Void> _handle;
  late final void Function(Pointer<Void>) _destroy;
  late final int Function(Pointer<Void>, Pointer<Utf8>) _open;
  late final int Function(Pointer<Void>, int) _setState, _seek;
  late final int Function(Pointer<Void>) _position, _duration, _status;
  late final Pointer<Utf8> Function(Pointer<Void>) _error;
  late final void Function(Pointer<Void>, double) _volumeFn;
  late final int Function(Pointer<Void>, double) _rateFn;
  late final int Function(Pointer<Void>, Pointer<Double>) _equalizer;
  late final Timer _timer;
  final _positions = StreamController<Duration>.broadcast();
  final _durations = StreamController<Duration?>.broadcast();
  final _states = StreamController<AudioPlaybackState>.broadcast();
  Completer<void>? _loading;
  bool _disposed = false;
  bool _wantsPlaying = false;
  bool _playing = false;
  int _lastStatus = -1;
  Duration _currentPosition = Duration.zero;
  Duration _currentDuration = Duration.zero;
  double _volume = 1;
  double _speed = 1;

  void _check() {
    if (_disposed) throw StateError(zhAppLocalizations.sfosPlayerClosed);
  }

  void _poll() {
    if (_disposed) return;
    final status = _status(_handle);
    if (status == 2 || status == 3) {
      if (_loading != null && !_loading!.isCompleted) _loading!.complete();
    }
    if (status == 5 && _lastStatus != 5) {
      final error = StateError(_error(_handle).toDartString());
      if (_loading != null && !_loading!.isCompleted) {
        _loading!.completeError(error);
      } else {
        _states.addError(error);
      }
    }
    _currentPosition = Duration(microseconds: _position(_handle));
    _positions.add(_currentPosition);
    final duration = Duration(microseconds: _duration(_handle));
    if (duration != _currentDuration) {
      _currentDuration = duration;
      _durations.add(duration);
    }
    final playing = _wantsPlaying && status != 0 && status != 4 && status != 5;
    if (status != _lastStatus || playing != _playing) {
      _lastStatus = status;
      _playing = playing;
      _states.add(
        AudioPlaybackState(
          playing: playing,
          processingState: switch (status) {
            1 =>
              _loading == null
                  ? ProcessingState.buffering
                  : ProcessingState.loading,
            2 || 3 => ProcessingState.ready,
            4 => ProcessingState.completed,
            _ => ProcessingState.idle,
          },
        ),
      );
    }
  }

  @override
  bool get playing => !_disposed && _playing;
  @override
  Duration get position => _currentPosition;
  @override
  double get volume => _volume;
  @override
  double get playbackSpeed => _speed;
  @override
  Stream<Duration> get positionStream => _positions.stream;
  @override
  Stream<Duration?> get durationStream => _durations.stream;
  @override
  Stream<AudioPlaybackState> get playerStateStream => _states.stream;

  @override
  Future<void> setUrl(String url) async {
    _check();
    if (_loading != null) {
      throw StateError(zhAppLocalizations.sfosTrackStillLoading);
    }
    final uri = url.startsWith('/') ? Uri.file(url).toString() : url;
    final native = uri.toNativeUtf8();
    final ready = Completer<void>();
    _loading = ready;
    _lastStatus = -1;
    _wantsPlaying = false;
    _currentPosition = Duration.zero;
    _currentDuration = Duration.zero;
    _durations.add(Duration.zero);
    _speed = 1;
    try {
      if (_open(_handle, native) == 0) {
        throw StateError(zhAppLocalizations.sfosCannotOpenAudio);
      }
      _poll();
      await ready.future.timeout(const Duration(seconds: 15));
    } catch (_) {
      if (!_disposed) _setState(_handle, 0);
      rethrow;
    } finally {
      calloc.free(native);
      _loading = null;
    }
  }

  Future<void> _state(int state) async {
    _check();
    if (_setState(_handle, state) == 0) {
      throw StateError(zhAppLocalizations.sfosCannotChangeState);
    }
    _wantsPlaying = state == 2;
    _poll();
  }

  @override
  Future<void> play() => _state(2);
  @override
  Future<void> pause() => _state(1);
  @override
  Future<void> stop() async {
    if (_loading != null && !_loading!.isCompleted) {
      _loading!.completeError(
        StateError(zhAppLocalizations.sfosLoadingCancelled),
      );
    }
    await _state(0);
    _currentPosition = Duration.zero;
    _positions.add(Duration.zero);
  }

  @override
  Future<void> seek(Duration position) async {
    _check();
    if (_seek(_handle, position.inMicroseconds) == 0) {
      throw StateError(zhAppLocalizations.sfosCannotSeek);
    }
    _poll();
  }

  @override
  Future<void> setVolume(double volume) async {
    _check();
    _volume = volume.clamp(0, 1);
    _volumeFn(_handle, _volume);
  }

  @override
  Future<void> setPlaybackSpeed(double speed) async {
    _check();
    if (speed < 0.25 || speed > 4) throw ArgumentError.value(speed, 'speed');
    if (_rateFn(_handle, speed) == 0) {
      throw StateError(zhAppLocalizations.sfosRateUnavailable);
    }
    _speed = speed;
  }

  @override
  Future<void> applyEqualizer({
    required bool enabled,
    required List<double> bandGains,
  }) async {
    _check();
    final bands = calloc<Double>(5);
    try {
      for (var i = 0; i < 5; i++) {
        bands[i] = enabled && i < bandGains.length ? bandGains[i] : 0;
      }
      if (_equalizer(_handle, bands) == 0 && enabled) {
        throw StateError(zhAppLocalizations.sfosEqualizerUnavailable);
      }
    } finally {
      calloc.free(bands);
    }
  }

  @override
  Future<void> dispose() async {
    if (_disposed) return;
    _disposed = true;
    _timer.cancel();
    if (_loading != null && !_loading!.isCompleted) {
      _loading!.completeError(StateError(zhAppLocalizations.sfosPlayerClosed));
    }
    _destroy(_handle);
    await Future.wait([
      _positions.close(),
      _durations.close(),
      _states.close(),
    ]);
  }
}
