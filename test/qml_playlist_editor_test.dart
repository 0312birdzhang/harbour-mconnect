import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/features/library/data/my_playlists_repository.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/models/song.dart';
import 'package:mconnect/models/playlist.dart';
import 'package:mconnect/platform/base/music_platform.dart';
import 'package:mconnect/platform/base/platform_registry.dart';
import 'package:mconnect/sailfish/qml_playlist_editor.dart';
import 'package:mconnect/sailfish/core_service.dart';

const a = Song(id: 'a', platform: PlatformType.netease, name: '甲', artists: []);
const b = Song(id: 'b', platform: PlatformType.netease, name: '乙', artists: []);

class ImportPlatform extends MusicPlatform {
  @override
  PlatformType get platformType => PlatformType.netease;
  String? link;
  @override
  Future<Playlist?> parseShareLink(String url) async {
    link = url;
    return const Playlist(
      id: 'remote',
      name: '平台歌单',
      platform: PlatformType.netease,
    );
  }

  @override
  Future<List<Song>> getPlaylistDetail(String id) async => const [a, b];
  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

void main() {
  late Directory temp;
  late MyPlaylistsRepository repository;
  late QmlPlaylistEditor editor;
  setUp(() async {
    temp = await Directory.systemTemp.createTemp('qml-playlist-');
    repository = MyPlaylistsRepository(storageDirectory: temp);
    editor = QmlPlaylistEditor(repository);
  });
  tearDown(() async {
    await temp.delete(recursive: true);
  });
  test(
    'concurrent writes retain both songs and moves use the latest order',
    () async {
      final list = await editor.create('歌单');
      await Future.wait([
        editor.edit({'action': 'add', 'id': list.id}, song: a),
        editor.edit({'action': 'add', 'id': list.id}, song: b),
      ]);
      expect((await repository.getSongs(list.id)).map((s) => s.id), ['a', 'b']);
      await editor.edit({
        'action': 'move',
        'id': list.id,
        'direction': 0,
      }, song: b);
      expect((await repository.getSongs(list.id)).map((s) => s.id), ['b', 'a']);
      await editor.edit({'action': 'remove', 'id': list.id}, song: a);
      expect((await repository.getSongs(list.id)).single.id, 'b');
    },
  );
  test(
    'rename duplicate export and import keep names and song identities',
    () async {
      final list = await editor.copy('原始', const [a, b]);
      await editor.edit({'action': 'rename', 'id': list.id, 'name': '重命名'});
      await editor.edit({'action': 'duplicate', 'id': list.id});
      final link = await editor.exportLink(list.id);
      final imported = await editor.importLink(link);
      expect(imported.name, '重命名');
      expect((await repository.getSongs(imported.id)).map((s) => s.id), [
        'a',
        'b',
      ]);
      expect((await repository.getPlaylists()).length, 3);
    },
  );
  test(
    'failed mutation does not poison later writes and deletion keeps audio',
    () async {
      await expectLater(
        editor.edit({'action': 'delete', 'id': 'missing'}),
        throwsA(isA<CoreRequestFailure>()),
      );
      final audio = File('${temp.path}/original.flac');
      await audio.writeAsString('original-audio');
      final list = await editor.copy('本地', [
        Song(
          id: audio.path,
          platform: PlatformType.local,
          name: '本地',
          artists: [],
        ),
      ]);
      await editor.edit({'action': 'delete', 'id': list.id});
      expect(await audio.readAsString(), 'original-audio');
      expect(await repository.getPlaylist(list.id), null);
    },
  );
  test(
    'platform share text resolves through the owning adapter into a local copy',
    () async {
      final platform = ImportPlatform();
      PlatformRegistry.register(platform);
      final imported = await editor.importLink(
        '分享歌单 https://music.163.com/playlist?id=123',
      );
      expect(platform.link, 'https://music.163.com/playlist?id=123');
      expect(imported.name, '平台歌单');
      expect((await repository.getSongs(imported.id)).length, 2);
      await expectLater(
        editor.importLink('https://example.test/playlist?id=123'),
        throwsA(isA<CoreRequestFailure>()),
      );
    },
  );
}
