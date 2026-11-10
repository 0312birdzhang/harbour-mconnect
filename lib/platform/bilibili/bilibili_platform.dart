import 'dart:convert';
import 'package:crypto/crypto.dart';
import 'package:dio/dio.dart';
import '../../core/network/api_exception.dart';
import '../../core/network/platform_http.dart';
import '../../core/storage/session_storage.dart';
import '../../models/artist.dart';
import '../../models/audio_quality.dart';
import '../../models/platform_type.dart';
import '../../models/playlist.dart';
import '../../models/song.dart';
import '../../models/user.dart';
import '../base/music_platform.dart';
import '../../sailfish/native_platforms.dart';

/// BiliMusic's web API flow, adapted for audio-only Sailfish playback.
/// Stable IDs are BV IDs (optionally followed by :cid for a specific part).
class BilibiliPlatform extends MusicPlatform {
  BilibiliPlatform({
    Dio? dio,
    DateTime Function()? now,
    NativePlatformClient? nativeClient,
  }) : _dio = dio ?? Dio(),
       _now = now ?? DateTime.now,
       _native = nativeClient ?? NativePlatformClient.forCurrentProcess();
  final Dio _dio;
  final NativePlatformClient? _native;
  int _nativeEpoch = 0;
  String? _nativeQrKey;
  int _nativeQrEpoch = -1;
  final DateTime Function() _now;
  final Map<String, String> _cookies = {};
  User? _user;
  String? _mixin;
  DateTime? _keyTime;
  static const userAgent =
      'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36';
  static const mediaHeaders = <String, String>{
    'Referer': 'https://www.bilibili.com/',
    'User-Agent': userAgent,
  };
  @override
  PlatformType get platformType => PlatformType.bilibili;
  @override
  String get platformName => '哔哩哔哩音乐';
  @override
  bool get isLoggedIn => _user != null && _cookies.containsKey('SESSDATA');

  String get _cookieHeader =>
      _cookies.entries.map((e) => '${e.key}=${e.value}').join('; ');
  Future<NativeBilibiliResult> _nativeCall(
    String operation, [
    Map<String, dynamic> args = const {},
  ]) async {
    final cookie = _cookieHeader, auth = _cookies['SESSDATA'];
    final epoch = _nativeEpoch;
    final NativeBilibiliResult result;
    try {
      result = await _native!.bilibili(operation, args, cookie: cookie);
    } on LoginExpiredException {
      if (epoch == _nativeEpoch && auth == _cookies['SESSDATA']) {
        SessionExpiryReporter.report(platformType);
      }
      rethrow;
    }
    if (epoch != _nativeEpoch || auth != _cookies['SESSDATA']) {
      throw RequestCancelledException();
    }
    if (result.cookie != null &&
        (cookie == _cookieHeader ||
            (operation == 'qrCheck' && result.qrStatus == 'success'))) {
      _cookies.clear();
      for (final part in result.cookie!.split(';')) {
        final pair = part.trim();
        final index = pair.indexOf('=');
        if (index > 0) {
          _cookies[pair.substring(0, index)] = pair.substring(index + 1);
        }
      }
    }
    if (operation == 'qrKey') {
      _nativeQrKey = result.qrKey;
      _nativeQrEpoch = epoch;
    }
    if (operation == 'qrCheck' && result.qrStatus == 'success') {
      if (!_cookies.containsKey('SESSDATA')) throw LoginExpiredException();
      _user = result.user;
    }
    return result;
  }

  Future<Response<dynamic>> _request(
    String path, {
    Map<String, dynamic>? params,
    Map<String, dynamic>? form,
  }) async {
    final response = await _dio.request<dynamic>(
      path.startsWith('https://') ? path : 'https://api.bilibili.com$path',
      queryParameters: params,
      data: form,
      options: Options(
        method: form == null ? 'GET' : 'POST',
        sendTimeout: const Duration(seconds: 20),
        receiveTimeout: const Duration(seconds: 25),
        contentType: form == null ? null : Headers.formUrlEncodedContentType,
        headers: {
          ...mediaHeaders,
          if (_cookies.isNotEmpty)
            'Cookie': _cookies.entries
                .map((e) => '${e.key}=${e.value}')
                .join('; '),
        },
      ),
    );
    for (final raw in response.headers['set-cookie'] ?? <String>[]) {
      final pair = raw.split(';').first;
      final split = pair.indexOf('=');
      if (split > 0) {
        _cookies[pair.substring(0, split)] = pair.substring(split + 1);
      }
    }
    return response;
  }

  Future<dynamic> _get(String path, [Map<String, dynamic>? params]) async {
    final response = await _request(path, params: params);
    final body = response.data is String
        ? jsonDecode(response.data as String)
        : response.data;
    if (body is! Map) throw ApiException(message: '哔哩哔哩返回了无效数据');
    if (body['code'] != 0) {
      if (body['code'] == -101) throw LoginExpiredException();
      throw ApiException(
        message: '哔哩哔哩：${body['message'] ?? body['code']}',
        statusCode: body['code'] as int?,
      );
    }
    return body['data'];
  }

  static const _order = [
    46,
    47,
    18,
    2,
    53,
    8,
    23,
    32,
    15,
    50,
    10,
    31,
    58,
    3,
    45,
    35,
    27,
    43,
    5,
    49,
    33,
    9,
    42,
    19,
    29,
    28,
    14,
    39,
    12,
    38,
    41,
    13,
  ];
  static Map<String, String> sign(
    Map<String, dynamic> params,
    String imageKey,
    String subKey,
    int timestamp,
  ) {
    final original = imageKey + subKey;
    final mixin = _order.map((i) => original[i]).join();
    return _sign(params, mixin, timestamp);
  }

  static Map<String, String> _sign(
    Map<String, dynamic> params,
    String mixin,
    int timestamp,
  ) {
    final result = <String, String>{
      for (final e in params.entries)
        e.key: '${e.value}'.replaceAll(RegExp(r"[!'()*]"), ''),
      'wts': '$timestamp',
    };
    final keys = result.keys.toList()..sort();
    String encode(String s) => Uri.encodeComponent(s)
        .replaceAll('!', '%21')
        .replaceAll("'", '%27')
        .replaceAll('(', '%28')
        .replaceAll(')', '%29')
        .replaceAll('*', '%2A');
    final query = keys
        .map((k) => '${encode(k)}=${encode(result[k]!)}')
        .join('&');
    result['w_rid'] = md5.convert(utf8.encode(query + mixin)).toString();
    return result;
  }

  Future<Map<String, String>> _signed(Map<String, dynamic> params) async {
    if (_mixin == null ||
        _now().difference(_keyTime!) > const Duration(hours: 12)) {
      // Anonymous nav returns -101 but still contains valid WBI keys.
      final response = await _request('/x/web-interface/nav');
      final body = response.data is String
          ? jsonDecode(response.data as String)
          : response.data;
      final images = body['data']?['wbi_img'];
      String key(dynamic url) =>
          Uri.parse('$url').pathSegments.last.split('.').first;
      if (images == null) throw ApiException(message: '无法获取哔哩哔哩签名，请稍后重试');
      final original = key(images['img_url']) + key(images['sub_url']);
      if (original.length < 59) throw ApiException(message: '哔哩哔哩签名数据无效');
      _mixin = _order.map((i) => original[i]).join();
      _keyTime = _now();
    }
    return _sign(params, _mixin!, _now().millisecondsSinceEpoch ~/ 1000);
  }

  static String _text(dynamic text) => '$text'
      .replaceAll(RegExp('<[^>]*>'), '')
      .replaceAll('&amp;', '&')
      .replaceAll('&quot;', '"')
      .replaceAll('&#39;', "'")
      .replaceAll('&lt;', '<')
      .replaceAll('&gt;', '>');
  static String _url(dynamic url) => url == null
      ? ''
      : '$url'.startsWith('//')
      ? 'https:$url'
      : '$url';
  Song _song(Map data) {
    final owner = data['owner'] ?? data['upper'] ?? {};
    final duration = '${data['duration'] ?? 0}';
    final seconds = duration
        .split(':')
        .fold<int>(0, (n, p) => n * 60 + (int.tryParse(p) ?? 0));
    return Song(
      id: '${data['bvid']}',
      platform: platformType,
      name: _text(data['title'] ?? ''),
      artists: [
        Artist(
          id: '${owner['mid'] ?? data['mid'] ?? ''}',
          name: '${owner['name'] ?? data['author'] ?? '未知UP主'}',
        ),
      ],
      duration: Duration(seconds: seconds),
      coverUrl: _url(data['pic'] ?? data['cover']),
    );
  }

  @override
  Future<List<Song>> search(
    String keyword, {
    int page = 1,
    int limit = 30,
  }) async {
    if (_native != null) {
      return (await _nativeCall('search', {
        'query': keyword,
        'page': page,
        'limit': limit.clamp(1, 50),
      })).songs;
    }
    if (!_cookies.containsKey('buvid3')) {
      await _get('/x/frontend/finger/spi').then((d) {
        if (d['b_3'] != null) _cookies['buvid3'] = '${d['b_3']}';
        if (d['b_4'] != null) _cookies['buvid4'] = '${d['b_4']}';
      });
    }
    var data = await _get(
      '/x/web-interface/wbi/search/type',
      await _signed({
        'search_type': 'video',
        'keyword': keyword,
        'page': page,
        'page_size': limit.clamp(1, 50),
        'order': 'totalrank',
        'platform': 'pc',
      }),
    );
    if (data['v_voucher'] != null) {
      data = await _get('/x/web-interface/search/type', {
        'search_type': 'video',
        'keyword': keyword,
        'page': page,
        'page_size': limit.clamp(1, 50),
        'order': 'totalrank',
      });
    }
    if (data['v_voucher'] != null) {
      throw ApiException(message: '哔哩哔哩需要安全验证，请扫码登录后重试');
    }
    return (data['result'] as List? ?? [])
        .whereType<Map>()
        .where((e) => '${e['bvid'] ?? ''}'.isNotEmpty)
        .map(_song)
        .toList();
  }

  Future<Map> _part(String id) async {
    final pieces = id.split(':');
    final pages =
        await _get('/x/player/pagelist', {'bvid': pieces.first}) as List;
    if (pages.isEmpty) throw SongNotAvailableException(platform: platformName);
    if (pieces.length > 1) {
      return pages.cast<Map>().firstWhere(
        (e) => '${e['cid']}' == pieces[1],
        orElse: () => throw SongNotAvailableException(platform: platformName),
      );
    }
    return pages.first as Map;
  }

  Future<List<Map>> _audio(String id) async {
    final part = await _part(id);
    final data = await _get(
      '/x/player/wbi/playurl',
      await _signed({
        'bvid': id.split(':').first,
        'cid': part['cid'],
        'fnval': 16,
        'fnver': 0,
        'qn': 80,
        'fourk': 0,
      }),
    );
    final audio = (data['dash']?['audio'] as List? ?? [])
        .whereType<Map>()
        .where((e) => _url(e['baseUrl'] ?? e['base_url']).isNotEmpty)
        .toList();
    audio.sort(
      (a, b) =>
          (a['bandwidth'] as num? ?? 0).compareTo(b['bandwidth'] as num? ?? 0),
    );
    if (audio.isEmpty) throw SongNotAvailableException(platform: platformName);
    return audio;
  }

  @override
  Future<String> getSongUrl(
    String songId, {
    AudioLevel quality = AudioLevel.low,
  }) async {
    if (_native != null) {
      return (await _nativeCall('stream', {
        'id': songId,
        'quality': quality.index,
      })).url!;
    }
    final tracks = await _audio(songId);
    final index = quality == AudioLevel.low
        ? 0
        : quality == AudioLevel.medium
        ? (tracks.length > 1 ? 1 : 0)
        : tracks.length - 1;
    return _url(tracks[index]['baseUrl'] ?? tracks[index]['base_url']);
  }

  @override
  Future<List<AudioQuality>> getAvailableQualities(String songId) async {
    if (_native != null) {
      return (await _nativeCall('qualities', {'id': songId})).qualities;
    }
    final tracks = await _audio(songId);
    return [
      for (var i = 0; i < tracks.length && i < 3; i++)
        AudioQuality(
          level: AudioLevel.values[i],
          bitrate: ((tracks[i]['bandwidth'] as num? ?? 0) / 1000).round(),
          format: 'm4a',
        ),
    ];
  }

  @override
  Future<QrLoginResult> getQrCode() async {
    if (_native != null) {
      _nativeEpoch++;
      final result = await _nativeCall('qrKey');
      return QrLoginResult(key: result.qrKey!, qrUrl: result.qrUrl);
    }
    final data = await _get(
      'https://passport.bilibili.com/x/passport-login/web/qrcode/generate',
    );
    return QrLoginResult(key: '${data['qrcode_key']}', qrUrl: '${data['url']}');
  }

  @override
  Stream<QrLoginStatus> pollQrStatus(String key) async* {
    if (_native != null) {
      final epoch = _nativeEpoch;
      if (key != _nativeQrKey || epoch != _nativeQrEpoch) return;
      for (var i = 0; i < 150 && epoch == _nativeEpoch; i++) {
        await Future<void>.delayed(const Duration(seconds: 2));
        if (epoch != _nativeEpoch) return;
        try {
          final result = await _nativeCall('qrCheck', {'key': key});
          if (epoch != _nativeEpoch) return;
          final status = QrLoginStatus.values.byName(result.qrStatus!);
          yield status;
          if (const [
            QrLoginStatus.success,
            QrLoginStatus.failed,
            QrLoginStatus.expired,
          ].contains(status)) {
            return;
          }
        } on RequestCancelledException {
          return;
        }
      }
      if (epoch == _nativeEpoch) yield QrLoginStatus.expired;
      return;
    }
    for (var i = 0; i < 150; i++) {
      await Future<void>.delayed(const Duration(seconds: 2));
      final data = await _get(
        'https://passport.bilibili.com/x/passport-login/web/qrcode/poll',
        {'qrcode_key': key},
      );
      switch (data['code']) {
        case 0:
          if (!_cookies.containsKey('SESSDATA')) {
            yield QrLoginStatus.failed;
            return;
          }
          _user = await _fetchUser();
          yield _user == null ? QrLoginStatus.failed : QrLoginStatus.success;
          return;
        case 86090:
          yield QrLoginStatus.scanned;
        case 86101:
          yield QrLoginStatus.waiting;
        case 86038:
          yield QrLoginStatus.expired;
          return;
        default:
          yield QrLoginStatus.failed;
          return;
      }
    }
    yield QrLoginStatus.expired;
  }

  Future<User?> _fetchUser() async {
    if (_native != null) return (await _nativeCall('profile')).user;
    final data = await _get('/x/web-interface/nav');
    if (data['isLogin'] != true) return null;
    return User(
      id: '${data['mid']}',
      nickname: '${data['uname']}',
      avatarUrl: _url(data['face']),
      platform: platformType,
      vipLevel: data['vipStatus'] == 1 ? VipLevel.vip : VipLevel.free,
    );
  }

  @override
  Future<User?> getUserInfo() async => _user;
  @override
  Future<LoginResult> loginByPhone(String phone, String code) async =>
      const LoginResult(success: false, error: '请使用哔哩哔哩客户端扫码登录');
  @override
  Future<void> logout() async {
    _nativeEpoch++;
    _nativeQrKey = null;
    _cookies.clear();
    _user = null;
    _mixin = null;
  }

  @override
  Future<void> saveSession(SessionStorage storage) async {
    await storage.saveCookie(platformType, jsonEncode(_cookies));
    if (_user != null) await storage.saveUser(platformType, _user!);
  }

  @override
  Future<void> restoreSession(SessionStorage storage) async {
    final epoch = ++_nativeEpoch;
    final raw = await storage.loadCookie(platformType);
    if (epoch != _nativeEpoch) return;
    if (raw == null) return;
    try {
      _cookies.addAll(Map<String, String>.from(jsonDecode(raw) as Map));
      final user = await storage.loadUser(platformType);
      if (epoch != _nativeEpoch) return;
      _user = user;
    } catch (_) {
      await logout();
    }
  }

  @override
  Future<List<Playlist>> getUserPlaylists() async {
    if (!isLoggedIn) throw LoginExpiredException();
    if (_native != null) {
      return (await _nativeCall('folders', {
        'uid': _user!.id,
        'nickname': _user!.nickname,
      })).playlists;
    }
    final result = <Playlist>[];
    for (var page = 1; ; page++) {
      final data = await _get('/x/v3/fav/folder/created/list', {
        'up_mid': _user!.id,
        'pn': page,
        'ps': 50,
      });
      final list = data['list'] as List? ?? [];
      result.addAll(
        list.cast<Map>().map(
          (e) => Playlist(
            id: '${e['id']}',
            name: '${e['title']}',
            platform: platformType,
            songCount: e['media_count'] as int? ?? 0,
            coverUrl: _url(e['cover']),
            creatorName: _user!.nickname,
            editable: true,
          ),
        ),
      );
      if (data['has_more'] != true || list.isEmpty) break;
    }
    return result;
  }

  @override
  Future<List<Song>> getPlaylistDetail(String playlistId) async {
    if (_native != null) {
      return (await _nativeCall('playlist', {'id': playlistId})).songs;
    }
    if (playlistId.startsWith('video:')) {
      final id = playlistId.substring(6);
      final data = await _get('/x/web-interface/view', {'bvid': id});
      return (data['pages'] as List? ?? [])
          .cast<Map>()
          .map(
            (p) => _song({
              ...data as Map,
              'bvid': '$id:${p['cid']}',
              'title': p['part'],
              'duration': p['duration'],
            }),
          )
          .toList();
    }
    final songs = <Song>[];
    for (var page = 1; ; page++) {
      final data = await _get('/x/v3/fav/resource/list', {
        'media_id': playlistId,
        'pn': page,
        'ps': 20,
        'platform': 'web',
      });
      final list = data['medias'] as List? ?? [];
      songs.addAll(
        list
            .whereType<Map>()
            .where(
              (e) =>
                  '${e['bvid'] ?? ''}'.isNotEmpty &&
                  ((e['attr'] as int? ?? 0) & 9) == 0,
            )
            .map(_song),
      );
      if (data['has_more'] != true || list.isEmpty) break;
    }
    return songs;
  }

  @override
  Future<List<Song>> getLikedSongs() async {
    // A Bilibili "like" is a video thumb-up, not a playlist. Do not substitute a favourite folder.
    throw UnsupportedActionException(
      platformName,
      details: '请在“我的歌单”中查看哔哩哔哩收藏夹',
    );
  }

  Future<bool> _favorite(String playlist, Song song, bool add) async {
    if (!isLoggedIn) throw LoginExpiredException();
    if (_native != null) {
      return (await _nativeCall(add ? 'add' : 'remove', {
        'id': playlist,
        'songId': song.id,
      })).ok;
    }
    final detail = await _get('/x/web-interface/view', {
      'bvid': song.id.split(':').first,
    });
    final response = await _request(
      '/x/v3/fav/resource/batch-deal',
      form: {
        'resources': '${detail['aid']}:2',
        'add_media_ids': add ? playlist : '',
        'del_media_ids': add ? '' : playlist,
        'csrf': _cookies['bili_jct'] ?? '',
        'platform': 'web',
      },
    );
    return response.data['code'] == 0;
  }

  @override
  Future<bool> addSongToPlaylist(String playlistId, Song song) =>
      _favorite(playlistId, song, true);
  @override
  Future<bool> removeSongFromPlaylist(String playlistId, Song song) =>
      _favorite(playlistId, song, false);
  @override
  Future<bool> likeSong(String songId, {bool like = true}) async {
    if (!isLoggedIn) throw LoginExpiredException();
    if (_native != null) {
      return (await _nativeCall('like', {'id': songId, 'enabled': like})).ok;
    }
    final data = await _get('/x/web-interface/view', {
      'bvid': songId.split(':').first,
    });
    final response = await _request(
      '/x/web-interface/archive/like',
      form: {
        'aid': data['aid'],
        'like': like ? 1 : 2,
        'csrf': _cookies['bili_jct'] ?? '',
      },
    );
    return response.data['code'] == 0;
  }

  @override
  Future<String?> getLyrics(String songId) async {
    if (_native != null) {
      return (await _nativeCall('lyrics', {'id': songId})).lyrics;
    }
    final part = await _part(songId);
    final data = await _get(
      '/x/player/wbi/v2',
      await _signed({'bvid': songId.split(':').first, 'cid': part['cid']}),
    );
    final subtitles = (data['subtitle']?['subtitles'] as List? ?? [])
        .cast<Map>();
    if (subtitles.isEmpty) return null;
    final chosen = subtitles.firstWhere(
      (e) => '${e['lan']}'.contains('zh'),
      orElse: () => subtitles.first,
    );
    // CDN subtitle requests carry no login cookies.
    final response = await _dio.get<dynamic>(
      _url(chosen['subtitle_url']),
      options: Options(headers: mediaHeaders),
    );
    final body = response.data is String
        ? jsonDecode(response.data as String)
        : response.data;
    final rows = body is List ? body : body['body'] as List? ?? [];
    return rows
        .map((e) {
          final cs = ((e['from'] as num) * 100).round();
          return '[${(cs ~/ 6000).toString().padLeft(2, '0')}:${(cs ~/ 100 % 60).toString().padLeft(2, '0')}.${(cs % 100).toString().padLeft(2, '0')}]${e['content']}';
        })
        .join('\n');
  }

  @override
  Future<List<Song>> getDailyRecommendations() async => const [];
  @override
  Future<List<Song>> getRankingList() async => const [];
  @override
  Future<VipLevel> getVipStatus() async => _user?.vipLevel ?? VipLevel.free;
  @override
  Future<Playlist?> parseShareLink(String url) async {
    if (_native != null) {
      final rows = (await _nativeCall('share', {'url': url})).playlists;
      return rows.isEmpty ? null : rows.first;
    }
    final uri = Uri.tryParse(url);
    if (uri == null ||
        !(uri.host == 'bilibili.com' || uri.host.endsWith('.bilibili.com'))) {
      return null;
    }
    final folder =
        uri.queryParameters['fid'] ?? uri.queryParameters['media_id'];
    if (folder != null && RegExp(r'^\d+$').hasMatch(folder)) {
      final data = await _get('/x/v3/fav/resource/list', {
        'media_id': folder,
        'pn': 1,
        'ps': 1,
      });
      final info = data['info'] as Map? ?? {};
      return Playlist(
        id: folder,
        name: '${info['title'] ?? '哔哩哔哩收藏夹'}',
        platform: platformType,
        songCount: info['media_count'] as int? ?? 0,
        coverUrl: _url(info['cover']),
      );
    }
    final match = RegExp(r'BV[0-9A-Za-z]+').firstMatch(uri.path);
    if (match == null) return null;
    final data = await _get('/x/web-interface/view', {'bvid': match[0]});
    return Playlist(
      id: 'video:${match[0]}',
      name: _text(data['title']),
      platform: platformType,
      songCount: (data['pages'] as List? ?? []).length,
      coverUrl: _url(data['pic']),
    );
  }
}
