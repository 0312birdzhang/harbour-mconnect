import '../features/library/data/my_playlists_repository.dart';
import '../models/song.dart';
import '../models/playlist.dart';
import '../platform/base/platform_registry.dart';
import 'core_service.dart';
import 'qml_library.dart';

/// Serializes local read/modify/write operations across native pages.
class QmlPlaylistEditor {
  QmlPlaylistEditor(this.local);
  final MyPlaylistsRepository local;
  Future<void> _tail = Future<void>.value();
  Future<T> write<T>(Future<T> Function() action) {
    final result = _tail.then((_) => action());
    _tail = result.then<void>((_) {}, onError: (Object _) {});
    return result;
  }

  static String name(Object? value) {
    final text = QmlLibrary.identifier(value).trim();
    if (text.isEmpty || text.length > 200) throw ArgumentError();
    return text;
  }

  Future<void> edit(Map<String, dynamic> args, {Song? song}) => write(() async {
    final id = QmlLibrary.identifier(args['id']);
    bool ok;
    switch (args['action']) {
      case 'rename':
        ok = await local.renamePlaylist(id, name(args['name']));
      case 'duplicate':
        ok = await local.duplicatePlaylist(id, nameSuffix: '副本') != null;
      case 'delete':
        ok = await local.deletePlaylist(id);
      case 'add':
        if (song == null) throw ArgumentError();
        ok = await local.addSong(id, song);
      case 'remove':
        if (song == null) throw ArgumentError();
        ok = await local.removeSong(id, song);
      case 'move':
        if (song == null) throw ArgumentError();
        final list = await local.getSongs(id);
        final index = list.indexOf(song);
        final direction = args['direction'];
        if (index < 0 || !const [-1, 0, 1].contains(direction)) {
          throw ArgumentError();
        }
        final target = direction == 0
            ? 0
            : (index + (direction as int)).clamp(0, list.length - 1);
        final item = list.removeAt(index);
        list.insert(target, item);
        ok = await local.reorderSongs(id, list);
      default:
        throw ArgumentError();
    }
    if (!ok) throw const CoreRequestFailure('歌单不存在或内容已变化，请刷新');
  });
  Future<Playlist> create(String title) =>
      write(() => local.createPlaylist(name(title)));
  Future<Playlist> copy(String title, List<Song> songs) =>
      write(() => local.importPlaylist(name: name(title), songs: songs));
  Future<String> exportLink(String id) async {
    await _tail;
    final link = await local.exportPlaylistLink(QmlLibrary.identifier(id));
    if (link == null) throw const CoreRequestFailure('歌单不存在，请刷新');
    if (link.length > 250000) throw const CoreRequestFailure('歌单较大，请拆分后导出');
    return link;
  }

  Future<Playlist> importLink(String link) async {
    if (link.isEmpty || link.length > 250000) throw ArgumentError();
    final trimmed = link.trim();
    final candidate = trimmed.startsWith('mconnect:')
        ? trimmed
        : RegExp(r'https?://[^\s]+').firstMatch(trimmed)?.group(0) ?? trimmed;
    final uri = Uri.tryParse(candidate);
    if (uri?.scheme == 'mconnect') {
      final playlist = await write(() => local.importShareLink(candidate));
      if (playlist == null) throw const CoreRequestFailure('歌单链接无效');
      return playlist;
    }
    final host = uri?.host.toLowerCase() ?? '';
    final domains = {
      'netease': ['music.163.com', '163cn.tv'],
      'qq': ['y.qq.com'],
      'kugou': ['kugou.com'],
      'bilibili': ['bilibili.com', 'b23.tv'],
    };
    final matches = domains.entries.where(
      (entry) => entry.value.any(
        (domain) => host == domain || host.endsWith('.$domain'),
      ),
    );
    if (matches.length != 1 || !const ['http', 'https'].contains(uri?.scheme)) {
      throw const CoreRequestFailure('请输入 Mconnect 或受支持平台的歌单链接');
    }
    final impl = PlatformRegistry.all.singleWhere(
      (p) => p.platformType.name == matches.single.key,
    );
    final playlist = await impl
        .parseShareLink(candidate)
        .timeout(const Duration(seconds: 8));
    if (playlist == null) throw const CoreRequestFailure('无法识别歌单链接，请使用完整链接');
    final songs = await impl
        .getPlaylistDetail(playlist.id)
        .timeout(const Duration(seconds: 12));
    if (songs.isEmpty) throw const CoreRequestFailure('未读取到歌曲，请检查链接或登录状态');
    return copy(playlist.name, songs);
  }
}
