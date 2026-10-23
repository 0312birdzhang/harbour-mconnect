import 'dart:async';
import 'dart:convert';
import 'package:crypto/crypto.dart';
import 'package:dbus/dbus.dart';
import '../core/diagnostics/diagnostics_service.dart';
import '../features/player/data/playback_notification_service.dart';
import '../models/song.dart';

const _rootInterface = 'org.mpris.MediaPlayer2';
const _playerInterface = 'org.mpris.MediaPlayer2.Player';

/// Session-bus MPRIS service consumed by Sailfish's system media controls.
class SailfishMpris extends DBusObject
    implements PlaybackNotificationController {
  SailfishMpris({DBusClient Function()? busFactory})
    : _busFactory = busFactory ?? DBusClient.session,
      super(DBusObjectPath('/org/mpris/MediaPlayer2'));
  final DBusClient Function() _busFactory;
  static final instance = SailfishMpris();
  PlaybackNotificationActions? _actions;
  DBusClient? _bus;
  Future<void>? _initializing;
  Future<void> Function()? onRaise;
  Song? _song;
  bool _playing = false;
  Duration _position = Duration.zero;
  Duration _duration = Duration.zero;

  DBusObjectPath get trackPath => DBusObjectPath(
    _song == null
        ? '/org/mpris/MediaPlayer2/TrackList/NoTrack'
        : '/org/mpris/MediaPlayer2/track/t${sha1.convert(utf8.encode('${_song!.platform.name}:${_song!.id}'))}',
  );

  Map<String, DBusValue> get _rootProperties => {
    'Identity': const DBusString('Mconnect'),
    'DesktopEntry': const DBusString('harbour-mconnect'),
    'CanQuit': const DBusBoolean(false),
    'CanRaise': DBusBoolean(onRaise != null),
    'HasTrackList': const DBusBoolean(false),
    'SupportedUriSchemes': DBusArray.string(['file', 'http', 'https']),
    'SupportedMimeTypes': DBusArray.string([
      'audio/mpeg',
      'audio/flac',
      'audio/ogg',
      'audio/mp4',
    ]),
  };

  Map<String, DBusValue> get _playerProperties => {
    'PlaybackStatus': DBusString(
      _song == null
          ? 'Stopped'
          : _playing
          ? 'Playing'
          : 'Paused',
    ),
    'Metadata': DBusDict.stringVariant({
      'mpris:trackid': trackPath,
      if (_song != null) ...{
        'xesam:title': DBusString(_song!.name),
        'xesam:artist': DBusArray.string([_song!.artistNames]),
        'mpris:length': DBusInt64(_duration.inMicroseconds),
        if (_song!.coverUrl != null)
          'mpris:artUrl': DBusString(_song!.coverUrl!),
      },
    }),
    'Position': DBusInt64(_position.inMicroseconds),
    'Rate': const DBusDouble(1),
    'MinimumRate': const DBusDouble(1),
    'MaximumRate': const DBusDouble(1),
    'CanGoNext': DBusBoolean(_actions != null && _song != null),
    'CanGoPrevious': DBusBoolean(_actions != null && _song != null),
    'CanPlay': DBusBoolean(_actions != null && _song != null),
    'CanPause': DBusBoolean(_actions != null && _song != null),
    'CanSeek': DBusBoolean(_song != null),
    'CanControl': const DBusBoolean(true),
  };

  @override
  List<DBusIntrospectInterface> introspect() => [
    DBusIntrospectInterface(
      _rootInterface,
      methods: [DBusIntrospectMethod('Raise')],
      properties: [
        for (final entry in _rootProperties.entries)
          DBusIntrospectProperty(
            entry.key,
            entry.value.signature,
            access: DBusPropertyAccess.read,
          ),
      ],
    ),
    DBusIntrospectInterface(
      _playerInterface,
      methods: [
        for (final name in [
          'Play',
          'Pause',
          'PlayPause',
          'Stop',
          'Next',
          'Previous',
        ])
          DBusIntrospectMethod(name),
        DBusIntrospectMethod(
          'Seek',
          args: [
            DBusIntrospectArgument(
              DBusSignature('x'),
              DBusArgumentDirection.in_,
            ),
          ],
        ),
        DBusIntrospectMethod(
          'SetPosition',
          args: [
            DBusIntrospectArgument(
              DBusSignature('o'),
              DBusArgumentDirection.in_,
            ),
            DBusIntrospectArgument(
              DBusSignature('x'),
              DBusArgumentDirection.in_,
            ),
          ],
        ),
      ],
      properties: [
        for (final entry in _playerProperties.entries)
          DBusIntrospectProperty(
            entry.key,
            entry.value.signature,
            access: DBusPropertyAccess.read,
          ),
      ],
      signals: [
        DBusIntrospectSignal(
          'Seeked',
          args: [
            DBusIntrospectArgument(
              DBusSignature('x'),
              DBusArgumentDirection.out,
            ),
          ],
        ),
      ],
    ),
  ];

  @override
  Future<DBusMethodResponse> getAllProperties(String interface) async =>
      interface == _rootInterface
      ? DBusGetAllPropertiesResponse(_rootProperties)
      : interface == _playerInterface
      ? DBusGetAllPropertiesResponse(_playerProperties)
      : DBusMethodErrorResponse.unknownInterface();

  @override
  Future<DBusMethodResponse> getProperty(String interface, String name) async {
    final value = (interface == _rootInterface
        ? _rootProperties
        : interface == _playerInterface
        ? _playerProperties
        : <String, DBusValue>{})[name];
    return value == null
        ? DBusMethodErrorResponse.unknownProperty()
        : DBusGetPropertyResponse(value);
  }

  @override
  Future<DBusMethodResponse> handleMethodCall(DBusMethodCall methodCall) async {
    if (methodCall.interface == _rootInterface &&
        methodCall.name == 'Raise' &&
        methodCall.values.isEmpty &&
        onRaise != null) {
      try {
        await onRaise!();
        return DBusMethodSuccessResponse();
      } catch (error) {
        return DBusMethodErrorResponse.failed(error.toString());
      }
    }
    final actions = _actions;
    if (methodCall.interface != _playerInterface) {
      return DBusMethodErrorResponse.unknownInterface();
    }
    if (actions == null) {
      return DBusMethodErrorResponse.failed('Player unavailable');
    }
    try {
      if (methodCall.name == 'Seek' &&
          methodCall.signature == DBusSignature('x')) {
        await _seek(
          _position + Duration(microseconds: methodCall.values.first.asInt64()),
        );
      } else if (methodCall.name == 'SetPosition' &&
          methodCall.signature == DBusSignature('ox')) {
        if (methodCall.values.first == trackPath) {
          final value = methodCall.values[1].asInt64();
          if (value >= 0 && value <= _duration.inMicroseconds) {
            await _seek(Duration(microseconds: value));
          }
        }
      } else if (methodCall.values.isEmpty) {
        switch (methodCall.name) {
          case 'Play':
            await actions.play();
          case 'Pause':
            await actions.pause();
          case 'PlayPause':
            await (_playing ? actions.pause() : actions.play());
          case 'Stop':
            await actions.pause();
            await actions.seek(Duration.zero);
          case 'Next':
            await actions.skipToNext();
          case 'Previous':
            await actions.skipToPrevious();
          default:
            return DBusMethodErrorResponse.unknownMethod();
        }
      } else {
        return DBusMethodErrorResponse.invalidArgs();
      }
      return DBusMethodSuccessResponse();
    } catch (error) {
      return DBusMethodErrorResponse.failed(error.toString());
    }
  }

  Future<void> _seek(Duration value) async {
    final target = Duration(
      microseconds: value.inMicroseconds.clamp(0, _duration.inMicroseconds),
    );
    await _actions!.seek(target);
    _position = target;
    await emitSignal(_playerInterface, 'Seeked', [
      DBusInt64(target.inMicroseconds),
    ]);
  }

  @override
  Future<void> initialize({DiagnosticsService? diagnostics}) =>
      _initializing ??= _start();

  Future<void> _start() async {
    final bus = _busFactory();
    _bus = bus;
    try {
      await bus.registerObject(this);
      final ownership = await bus.requestName(
        'org.mpris.MediaPlayer2.harbour_mconnect',
        flags: {DBusRequestNameFlag.doNotQueue},
      );
      if (ownership != DBusRequestNameReply.primaryOwner &&
          ownership != DBusRequestNameReply.alreadyOwner) {
        throw StateError('Mconnect MPRIS name already in use');
      }
    } catch (error, stack) {
      await bus.close();
      if (_bus == bus) _bus = null;
      DiagnosticsService.instance.recordError('sailfish.mpris', error, stack);
    }
  }

  @override
  void attach(PlaybackNotificationActions actions) {
    _actions = actions;
    unawaited(initialize());
  }

  @override
  void detach() {
    _actions = null;
    final bus = _bus;
    _bus = null;
    _initializing = null;
    if (bus != null) unawaited(bus.close());
  }

  @override
  void update({
    required Song? currentSong,
    required List<Song> playlist,
    required int currentIndex,
    required bool isCurrentSongLiked,
    required bool isFloatingLyricsEnabled,
    required bool isPlaying,
    required Duration position,
    required Duration duration,
  }) {
    final changed =
        currentSong?.id != _song?.id ||
        currentSong?.platform != _song?.platform ||
        isPlaying != _playing ||
        duration != _duration;
    _song = currentSong;
    _playing = isPlaying;
    _position = position;
    _duration = duration;
    if (changed && _bus != null) {
      unawaited(
        emitPropertiesChanged(
          _playerInterface,
          changedProperties: Map.of(_playerProperties)..remove('Position'),
        ).catchError((Object error, StackTrace stack) {
          DiagnosticsService.instance.recordError(
            'sailfish.mpris.update',
            error,
            stack,
          );
        }),
      );
    }
  }
}
