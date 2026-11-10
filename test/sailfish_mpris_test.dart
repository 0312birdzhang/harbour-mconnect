import 'dart:io';
import 'dart:async';
import 'package:dbus/dbus.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/sailfish/mpris.dart';
import 'package:mconnect/features/player/data/playback_notification_service.dart';
import 'package:mconnect/models/song.dart';
import 'package:mconnect/models/platform_type.dart';

class NativeArtworkMock extends DBusObject {
  NativeArtworkMock()
    : super(DBusObjectPath('/org/sailfishos/Mconnect/Artwork'));
  final requested = Completer<String>();
  @override
  Future<DBusMethodResponse> handleMethodCall(DBusMethodCall call) async {
    if (call.interface != 'org.sailfishos.Mconnect.Artwork' ||
        call.name != 'MediaUrl') {
      return DBusMethodErrorResponse.unknownMethod();
    }
    requested.complete((call.values.single as DBusString).value);
    return DBusMethodSuccessResponse([
      const DBusString('http://127.0.0.1:12345/private/native-artwork'),
    ]);
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test(
    'MPRIS publishes track metadata and dispatches system controls over D-Bus',
    () async {
      final directory = await Directory.systemTemp.createTemp(
        'mconnect-mpris-',
      );
      final server = DBusServer();
      final address = await server.listenAddress(
        DBusAddress.unix(dir: directory),
      );
      final serviceBus = DBusClient(address);
      final remoteBus = DBusClient(address);
      final nativeArtwork = NativeArtworkMock();
      await remoteBus.registerObject(nativeArtwork);
      await remoteBus.requestName('org.sailfishos.Mconnect.Silica');
      final service = SailfishMpris(busFactory: () => serviceBus);
      addTearDown(() async {
        service.detach();
        await remoteBus.close();
        await serviceBus.close();
        await server.close();
        await directory.delete(recursive: true);
      });
      var raises = 0;
      service.onRaise = () async {
        raises++;
      };
      var plays = 0;
      var pauses = 0;
      var next = 0;
      var seek = Duration.zero;
      service.attach(
        PlaybackNotificationActions(
          play: () async {
            plays++;
          },
          pause: () async {
            pauses++;
          },
          skipToNext: () async {
            next++;
          },
          skipToPrevious: () async {},
          seek: (value) async {
            seek = value;
          },
          toggleLikeCurrentSong: () async {},
          toggleFloatingLyrics: () async {},
        ),
      );
      await service.initialize();
      const song = Song(
        id: 'id/with-chinese-中文',
        platform: PlatformType.netease,
        name: '测试歌曲',
        artists: [],
        coverUrl: 'https://image.test/cover.png',
      );
      service.update(
        currentSong: song,
        playlist: [song],
        currentIndex: 0,
        isCurrentSongLiked: false,
        isFloatingLyricsEnabled: false,
        isPlaying: true,
        position: const Duration(seconds: 10),
        duration: const Duration(minutes: 3),
      );
      final object = DBusRemoteObject(
        remoteBus,
        name: 'org.mpris.MediaPlayer2.harbour_mconnect',
        path: DBusObjectPath('/org/mpris/MediaPlayer2'),
      );
      expect(
        await nativeArtwork.requested.future.timeout(
          const Duration(seconds: 3),
        ),
        song.coverUrl,
      );
      // Let the asynchronous metadata update finish after MediaUrl replies.
      await Future<void>.delayed(const Duration(milliseconds: 20));
      final metadata = await object.getProperty(
        'org.mpris.MediaPlayer2.Player',
        'Metadata',
      );
      expect(
        metadata.asStringVariantDict()['xesam:title'],
        const DBusString('测试歌曲'),
      );
      expect(
        metadata.asStringVariantDict()['mpris:artUrl'],
        const DBusString('http://127.0.0.1:12345/private/native-artwork'),
      );
      expect(
        service.trackPath.value,
        matches(RegExp(r'^/org/mpris/MediaPlayer2/track/t[a-f0-9]+$')),
      );
      await object.callMethod('org.mpris.MediaPlayer2', 'Raise', []);
      expect(raises, 1);
      expect(plays, 0);
      await object.callMethod('org.mpris.MediaPlayer2.Player', 'Play', []);
      await object.callMethod('org.mpris.MediaPlayer2.Player', 'Pause', []);
      await object.callMethod('org.mpris.MediaPlayer2.Player', 'Next', []);
      await object.callMethod('org.mpris.MediaPlayer2.Player', 'Seek', [
        const DBusInt64(-5000000),
      ]);
      expect(plays, 1);
      expect(pauses, 1);
      expect(next, 1);
      expect(seek, const Duration(seconds: 5));
      await object.callMethod('org.mpris.MediaPlayer2.Player', 'SetPosition', [
        DBusObjectPath('/stale_track'),
        const DBusInt64(20000000),
      ]);
      expect(seek, const Duration(seconds: 5));
      await object.callMethod('org.mpris.MediaPlayer2.Player', 'SetPosition', [
        service.trackPath,
        const DBusInt64(20000000),
      ]);
      expect(seek, const Duration(seconds: 20));
    },
  );
}
