import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:dbus/dbus.dart';
import '../core/network/api_exception.dart';
import '../models/album.dart';
import '../models/user.dart';
import '../models/audio_quality.dart';
import '../models/toplist.dart';
import '../lyrics/models/lyrics_bundle.dart';
import '../models/artist.dart';
import '../models/platform_type.dart';
import '../models/playlist.dart';
import '../models/song.dart';

/// Transitional decoder only: networking and provider JSON parsing live in C++.
/// No native error silently retries through the legacy Dart HTTP implementation.
class NativePlatformClient {
  NativePlatformClient({DBusClient? bus}) : _injectedBus = bus;
  final DBusClient? _injectedBus;
  late final DBusClient _bus = _injectedBus ?? DBusClient.session();
  static NativePlatformClient? forCurrentProcess() =>
      Platform.isLinux && Platform.environment['MCONNECT_SILICA_CORE'] == '1'
      ? NativePlatformClient()
      : null;

  /// Shared normalized catalog wire records; provider parsing stays in C++.
  Future<NativeQqResult> kugouCatalog(
    String operation,
    Map<String, dynamic> arguments,
  ) async {
    if (!const [
          'search',
          'artist',
          'artistSongs',
          'artistAlbums',
          'album',
          'albumSongs',
          'toplists',
          'ranked',
          'homepage',
          'recommend',
        ].contains(operation) ||
        utf8.encode(jsonEncode(arguments)).length > 16384) {
      throw ArgumentError('Invalid native Kugou arguments');
    }
    try {
      final remote = DBusRemoteObject(
        _bus,
        name: 'org.sailfishos.Mconnect.Silica',
        path: DBusObjectPath('/org/sailfishos/Mconnect/Platforms'),
      );
      final reply = await remote
          .callMethod(
            'org.sailfishos.Mconnect.Platforms',
            'KugouCatalog',
            [DBusString(operation), DBusString(jsonEncode(arguments))],
            replySignature: DBusSignature('s'),
          )
          .timeout(const Duration(seconds: 19));
      final raw = (reply.returnValues.single as DBusString).value;
      if (utf8.encode(raw).length > 8 * 1024 * 1024) {
        throw const FormatException();
      }
      final data = jsonDecode(raw);
      if (data is! Map<String, dynamic> ||
          data['backend'] != 'cpp-kugou-catalog') {
        throw const FormatException();
      }
      if (data['error'] != null) {
        throw ApiException(
          message: '酷狗原生接口请求失败，请稍后重试',
          code: data['error'] == 'timeout'
              ? ApiErrorCode.requestTimeout
              : ApiErrorCode.requestFailed,
        );
      }
      if (!const [
            'artist',
            'artistAlbums',
            'album',
            'toplists',
          ].contains(operation) &&
          data['songs'] is! List) {
        throw const FormatException();
      }
      return NativeQqResult.fromJson(
        data,
        operation,
        platform: PlatformType.kugou,
      );
    } on ApiException {
      rethrow;
    } on TimeoutException {
      throw ApiException(
        message: '酷狗请求超时，请稍后重试',
        code: ApiErrorCode.requestTimeout,
      );
    } catch (_) {
      throw ApiException(
        message: '酷狗原生接口暂不可用，请重新打开应用',
        code: ApiErrorCode.requestFailed,
      );
    }
  }

  Future<NativeQqResult> kugouContent(
    String operation,
    Map<String, dynamic> arguments, {
    required String session,
  }) async {
    if (!const [
      'searchPlaylists',
      'stream',
      'qualities',
      'lyrics',
      'lyricsByInfo',
    ].contains(operation)) {
      throw ArgumentError('Invalid native Kugou content operation');
    }
    return _kugouRequest(
      operation,
      arguments,
      method: 'KugouContent',
      backend: 'cpp-kugou-content',
      session: session,
      decode: (data) => NativeQqResult.fromJson(
        data,
        operation == 'lyricsByInfo' ? 'lyrics' : operation,
        platform: PlatformType.kugou,
      ),
    );
  }

  Future<NativeKugouAccount> kugouAccount(
    String operation,
    Map<String, dynamic> arguments, {
    required String session,
  }) async {
    if (!const [
      'qrKey',
      'qrCheck',
      'qrCancel',
      'profile',
      'vip',
    ].contains(operation)) {
      throw ArgumentError('Invalid native Kugou account operation');
    }
    return _kugouRequest(
      operation,
      arguments,
      method: 'KugouAccount',
      backend: 'cpp-kugou-account',
      session: session,
      maximumBytes: 2 * 1024 * 1024,
      decode: (data) => NativeKugouAccount.fromJson(data, operation),
    );
  }

  Future<NativeKugouLibrary> kugouLibrary(
    String operation,
    Map<String, dynamic> arguments, {
    required String session,
  }) async {
    if (!const [
      'userPlaylists',
      'playlist',
      'liked',
      'like',
      'create',
      'collect',
      'share',
    ].contains(operation)) {
      throw ArgumentError('Invalid native Kugou library operation');
    }
    return _kugouRequest(
      operation,
      arguments,
      method: 'KugouLibrary',
      backend: 'cpp-kugou-library',
      session: session,
      decode: (data) => NativeKugouLibrary.fromJson(data, operation),
    );
  }

  Future<T> _kugouRequest<T>(
    String operation,
    Map<String, dynamic> arguments, {
    required String method,
    required String backend,
    required String session,
    required T Function(Map<String, dynamic>) decode,
    int maximumBytes = 8 * 1024 * 1024,
  }) async {
    if (utf8.encode(jsonEncode(arguments)).length > 16384 ||
        utf8.encode(session).length > 32768) {
      throw ArgumentError('Invalid native Kugou content arguments');
    }
    try {
      final remote = DBusRemoteObject(
        _bus,
        name: 'org.sailfishos.Mconnect.Silica',
        path: DBusObjectPath('/org/sailfishos/Mconnect/Platforms'),
      );
      final reply = await remote
          .callMethod('org.sailfishos.Mconnect.Platforms', method, [
            DBusString(operation),
            DBusString(jsonEncode(arguments)),
            DBusString(session),
          ], replySignature: DBusSignature('s'))
          .timeout(const Duration(seconds: 19));
      final raw = (reply.returnValues.single as DBusString).value;
      if (utf8.encode(raw).length > maximumBytes) throw const FormatException();
      final data = jsonDecode(raw);
      if (data is! Map<String, dynamic> || data['backend'] != backend) {
        throw const FormatException();
      }
      if (data['error'] == 'login_required') throw LoginExpiredException();
      if (data['error'] == 'cancelled') throw RequestCancelledException();
      if (data['error'] == 'unavailable') {
        throw SongNotAvailableException(platform: '酷狗音乐');
      }
      if (data['error'] != null) {
        throw ApiException(
          message: '酷狗原生接口请求失败，请稍后重试',
          code: data['error'] == 'timeout'
              ? ApiErrorCode.requestTimeout
              : ApiErrorCode.requestFailed,
        );
      }
      return decode(data);
    } on ApiException {
      rethrow;
    } on TimeoutException {
      throw ApiException(
        message: '酷狗请求超时，请稍后重试',
        code: ApiErrorCode.requestTimeout,
      );
    } catch (_) {
      throw ApiException(
        message: '酷狗原生接口暂不可用，请重新打开应用',
        code: ApiErrorCode.requestFailed,
      );
    }
  }

  Future<NativeQqAccount> qqAccount(
    String operation,
    Map<String, dynamic> arguments, {
    required String cookie,
  }) async {
    if (!const ['qrKey', 'qrCheck', 'qrCancel'].contains(operation) ||
        utf8.encode(jsonEncode(arguments)).length > 16384 ||
        cookie.length > 32768 ||
        cookie.codeUnits.any((c) => c < 32 || c > 126)) {
      throw ArgumentError('Invalid native QQ account arguments');
    }
    try {
      final remote = DBusRemoteObject(
        _bus,
        name: 'org.sailfishos.Mconnect.Silica',
        path: DBusObjectPath('/org/sailfishos/Mconnect/Platforms'),
      );
      final reply = await remote
          .callMethod('org.sailfishos.Mconnect.Platforms', 'QqAccount', [
            DBusString(operation),
            DBusString(jsonEncode(arguments)),
            DBusString(cookie),
          ], replySignature: DBusSignature('s'))
          .timeout(const Duration(seconds: 19));
      final raw = (reply.returnValues.single as DBusString).value;
      if (utf8.encode(raw).length > 2 * 1024 * 1024) {
        throw const FormatException();
      }
      final data = jsonDecode(raw);
      if (data is! Map<String, dynamic> ||
          data['backend'] != 'cpp-qq-account') {
        throw const FormatException();
      }
      if (data['error'] == 'cancelled') throw RequestCancelledException();
      if (data['error'] != null) {
        throw ApiException(
          message: data['error'] == 'verification_required'
              ? 'QQ音乐需要重新扫码验证，请重试'
              : 'QQ音乐原生登录请求失败，请稍后重试',
          code: data['error'] == 'timeout'
              ? ApiErrorCode.requestTimeout
              : ApiErrorCode.requestFailed,
        );
      }
      return NativeQqAccount.fromJson(data, operation);
    } on ApiException {
      rethrow;
    } on TimeoutException {
      throw ApiException(
        message: 'QQ音乐登录请求超时，请稍后重试',
        code: ApiErrorCode.requestTimeout,
      );
    } catch (_) {
      throw ApiException(
        message: 'QQ音乐原生登录暂不可用，请重新打开应用',
        code: ApiErrorCode.requestFailed,
      );
    }
  }

  Future<NativeQqResult> qqCatalog(
    String operation,
    Map<String, dynamic> arguments, {
    required String cookie,
  }) async {
    if (!const [
          'search',
          'searchPlaylists',
          'stream',
          'qualities',
          'artist',
          'artistSongs',
          'artistAlbums',
          'album',
          'albumSongs',
          'newSongs',
          'toplists',
          'ranked',
          'lyrics',
          'playlist',
          'playlistInfo',
          'liked',
          'profile',
          'vip',
          'userPlaylists',
          'like',
          'add',
          'create',
          'collect',
          'dailyId',
        ].contains(operation) ||
        utf8.encode(jsonEncode(arguments)).length > 16384 ||
        cookie.length > 32768 ||
        cookie.codeUnits.any((c) => c < 32 || c > 126)) {
      throw ArgumentError('Invalid native QQ arguments');
    }
    try {
      final remote = DBusRemoteObject(
        _bus,
        name: 'org.sailfishos.Mconnect.Silica',
        path: DBusObjectPath('/org/sailfishos/Mconnect/Platforms'),
      );
      final reply = await remote
          .callMethod(
            'org.sailfishos.Mconnect.Platforms',
            'QqCatalog',
            [
              DBusString(operation),
              DBusString(jsonEncode(arguments)),
              DBusString(cookie),
            ],
            replySignature: DBusSignature('s'),
          )
          .timeout(const Duration(seconds: 19));
      final raw = (reply.returnValues.single as DBusString).value;
      if (utf8.encode(raw).length > 8 * 1024 * 1024) {
        throw const FormatException();
      }
      final data = jsonDecode(raw);
      if (data is! Map<String, dynamic> ||
          data['backend'] != 'cpp-qq-catalog') {
        throw const FormatException();
      }
      if (data['error'] == 'login_required') throw LoginExpiredException();
      if (data['error'] == 'unavailable') {
        throw SongNotAvailableException(platform: 'QQ音乐');
      }
      if (data['error'] != null) {
        throw ApiException(
          message: 'QQ音乐原生接口请求失败，请稍后重试',
          code: data['error'] == 'timeout'
              ? ApiErrorCode.requestTimeout
              : ApiErrorCode.requestFailed,
        );
      }
      return NativeQqResult.fromJson(data, operation);
    } on ApiException {
      rethrow;
    } on TimeoutException {
      throw ApiException(
        message: 'QQ音乐请求超时，请稍后重试',
        code: ApiErrorCode.requestTimeout,
      );
    } catch (_) {
      throw ApiException(
        message: 'QQ音乐原生接口暂不可用，请重新打开应用',
        code: ApiErrorCode.requestFailed,
      );
    }
  }

  Future<NativeBilibiliResult> bilibili(
    String operation,
    Map<String, dynamic> arguments, {
    required String cookie,
  }) async {
    if (!const [
          'search',
          'stream',
          'qualities',
          'qrKey',
          'qrCheck',
          'profile',
          'folders',
          'playlist',
          'like',
          'add',
          'remove',
          'lyrics',
          'share',
        ].contains(operation) ||
        utf8.encode(jsonEncode(arguments)).length > 16384 ||
        cookie.length > 32768 ||
        cookie.codeUnits.any((c) => c < 32 || c > 126)) {
      throw ArgumentError('Invalid native Bilibili arguments');
    }
    try {
      final remote = DBusRemoteObject(
        _bus,
        name: 'org.sailfishos.Mconnect.Silica',
        path: DBusObjectPath('/org/sailfishos/Mconnect/Platforms'),
      );
      final reply = await remote
          .callMethod(
            'org.sailfishos.Mconnect.Platforms',
            'Bilibili',
            [
              DBusString(operation),
              DBusString(jsonEncode(arguments)),
              DBusString(cookie),
            ],
            replySignature: DBusSignature('s'),
          )
          .timeout(const Duration(seconds: 19));
      final raw = (reply.returnValues.single as DBusString).value;
      if (utf8.encode(raw).length > 8 * 1024 * 1024) {
        throw const FormatException();
      }
      final data = jsonDecode(raw);
      if (data is! Map<String, dynamic> || data['backend'] != 'cpp-bilibili') {
        throw const FormatException();
      }
      if (data['error'] == 'login_required') throw LoginExpiredException();
      if (data['error'] == 'unavailable') {
        throw SongNotAvailableException(platform: '哔哩哔哩音乐');
      }
      if (data['error'] != null) {
        throw ApiException(
          message: data['error'] == 'verification_required'
              ? '哔哩哔哩需要安全验证，请扫码登录后重试'
              : '哔哩哔哩原生接口请求失败，请稍后重试',
          code: data['error'] == 'timeout'
              ? ApiErrorCode.requestTimeout
              : ApiErrorCode.requestFailed,
        );
      }
      return NativeBilibiliResult.fromJson(data, operation);
    } on ApiException {
      rethrow;
    } on TimeoutException {
      throw ApiException(
        message: '哔哩哔哩请求超时，请稍后重试',
        code: ApiErrorCode.requestTimeout,
      );
    } catch (_) {
      throw ApiException(
        message: '哔哩哔哩原生接口暂不可用，请重新打开应用',
        code: ApiErrorCode.requestFailed,
      );
    }
  }

  Future<NativeNeteaseAccount> accountNetease(
    String operation,
    Map<String, dynamic> arguments, {
    required String cookie,
  }) async {
    if (!const [
          'qrKey',
          'qrCheck',
          'sms',
          'phoneLogin',
          'profile',
          'vip',
          'like',
          'addTrack',
          'removeTrack',
          'create',
          'rename',
          'delete',
          'collect',
        ].contains(operation) ||
        utf8.encode(jsonEncode(arguments)).length > 16384 ||
        cookie.length > 32768 ||
        cookie.codeUnits.any((c) => c < 32 || c > 126)) {
      throw ArgumentError('Invalid native account arguments');
    }
    try {
      final remote = DBusRemoteObject(
        _bus,
        name: 'org.sailfishos.Mconnect.Silica',
        path: DBusObjectPath('/org/sailfishos/Mconnect/Platforms'),
      );
      final reply = await remote
          .callMethod(
            'org.sailfishos.Mconnect.Platforms',
            'NeteaseAccount',
            [
              DBusString(operation),
              DBusString(jsonEncode(arguments)),
              DBusString(cookie),
            ],
            replySignature: DBusSignature('s'),
          )
          .timeout(const Duration(seconds: 19));
      final raw = (reply.returnValues.single as DBusString).value;
      if (utf8.encode(raw).length > 1024 * 1024) throw const FormatException();
      final data = jsonDecode(raw);
      if (data is! Map<String, dynamic> ||
          data['backend'] != 'cpp-netease-account') {
        throw const FormatException();
      }
      if (data['error'] == 'login_required') throw LoginExpiredException();
      if (data['error'] != null) {
        throw ApiException(
          message: '网易云音乐账号接口请求失败，请稍后重试',
          code: data['error'] == 'timeout'
              ? ApiErrorCode.requestTimeout
              : ApiErrorCode.requestFailed,
        );
      }
      return NativeNeteaseAccount.fromJson(data, operation);
    } on ApiException {
      rethrow;
    } on TimeoutException {
      throw ApiException(
        message: '网易云音乐请求超时，请稍后重试',
        code: ApiErrorCode.requestTimeout,
      );
    } catch (_) {
      throw ApiException(
        message: '网易云音乐原生账号接口暂不可用，请重新打开应用',
        code: ApiErrorCode.requestFailed,
      );
    }
  }

  Future<NativeNeteaseCatalog> catalogNetease(
    String operation,
    String id, {
    int offset = 0,
    int limit = 100,
    required String cookie,
  }) async {
    if (!const [
          'playlist',
          'playlistInfo',
          'userPlaylists',
          'daily',
          'toplists',
          'ranked',
          'newSongs',
          'lyrics',
          'stream',
          'qualities',
        ].contains(operation) ||
        !RegExp(r'^[0-9]{1,32}$').hasMatch(id) ||
        offset < 0 ||
        offset > 10000 ||
        limit < 1 ||
        limit > 10000 ||
        offset + limit > 20000 ||
        cookie.length > 32768 ||
        cookie.codeUnits.any((c) => c < 32 || c > 126)) {
      throw ArgumentError('Invalid catalog arguments');
    }
    try {
      final remote = DBusRemoteObject(
        _bus,
        name: 'org.sailfishos.Mconnect.Silica',
        path: DBusObjectPath('/org/sailfishos/Mconnect/Platforms'),
      );
      final reply = await remote
          .callMethod(
            'org.sailfishos.Mconnect.Platforms',
            'NeteaseCatalog',
            [
              DBusString(operation),
              DBusString(id),
              DBusInt32(offset),
              DBusInt32(limit),
              DBusString(cookie),
            ],
            replySignature: DBusSignature('s'),
          )
          .timeout(const Duration(seconds: 26));
      final raw = (reply.returnValues.single as DBusString).value;
      if (utf8.encode(raw).length > 8 * 1024 * 1024) {
        throw const FormatException();
      }
      final data = jsonDecode(raw);
      if (data is! Map<String, dynamic> ||
          data['backend'] != 'cpp-netease-catalog') {
        throw const FormatException();
      }
      if (data['error'] == 'login_required') throw LoginExpiredException();
      if (data['error'] == 'vip_required') {
        throw NoVipMembershipException('网易云音乐');
      }
      if (data['error'] == 'unavailable') {
        throw SongNotAvailableException(platform: '网易云音乐');
      }
      if (data['error'] != null) {
        throw ApiException(
          message: '网易云音乐原生接口请求失败，请稍后重试',
          code: data['error'] == 'timeout'
              ? ApiErrorCode.requestTimeout
              : ApiErrorCode.requestFailed,
        );
      }
      return NativeNeteaseCatalog.fromJson(data, operation);
    } on ApiException {
      rethrow;
    } on TimeoutException {
      throw ApiException(
        message: '网易云音乐请求超时，请稍后重试',
        code: ApiErrorCode.requestTimeout,
      );
    } catch (_) {
      throw ApiException(
        message: '网易云音乐原生接口暂不可用，请重新打开应用',
        code: ApiErrorCode.requestFailed,
      );
    }
  }

  Future<NativeNeteaseResult> searchNetease(
    String query, {
    required int page,
    required int limit,
    required bool playlists,
    required String cookie,
  }) async {
    if (query.trim().isEmpty ||
        query.length > 256 ||
        page < 1 ||
        page > 100 ||
        limit < 1 ||
        limit > 50 ||
        cookie.length > 32768 ||
        cookie.codeUnits.any((value) => value < 32 || value > 126)) {
      throw ArgumentError('Invalid native search arguments');
    }
    try {
      final object = DBusRemoteObject(
        _bus,
        name: 'org.sailfishos.Mconnect.Silica',
        path: DBusObjectPath('/org/sailfishos/Mconnect/Platforms'),
      );
      final reply = await object
          .callMethod(
            'org.sailfishos.Mconnect.Platforms',
            'SearchNetease',
            [
              DBusString(query.trim()),
              DBusInt32(page),
              DBusInt32(limit),
              DBusInt32(playlists ? 1000 : 1),
              DBusString(cookie),
            ],
            replySignature: DBusSignature('s'),
          )
          .timeout(const Duration(seconds: 19));
      final raw = (reply.returnValues.single as DBusString).value;
      if (utf8.encode(raw).length > 4 * 1024 * 1024) {
        throw const FormatException();
      }
      final data = jsonDecode(raw);
      if (data is! Map<String, dynamic> ||
          data['backend'] != 'cpp-netease-search') {
        throw const FormatException();
      }
      if (data['error'] == 'login_required') {
        throw LoginExpiredException();
      }
      if (data['error'] != null) {
        throw ApiException(
          message: data['error'] == 'timeout'
              ? '网易云音乐请求超时，请稍后重试'
              : '网易云音乐原生接口请求失败，请稍后重试',
          code: data['error'] == 'timeout'
              ? ApiErrorCode.requestTimeout
              : ApiErrorCode.requestFailed,
        );
      }
      return NativeNeteaseResult.fromJson(data, playlists: playlists);
    } on ApiException {
      rethrow;
    } on TimeoutException {
      throw ApiException(
        message: '网易云音乐请求超时，请稍后重试',
        code: ApiErrorCode.requestTimeout,
      );
    } catch (_) {
      throw ApiException(
        message: '网易云音乐原生接口暂不可用，请重新打开应用',
        code: ApiErrorCode.requestFailed,
      );
    }
  }

  Future<NativeNeteaseContent> contentNetease(
    String operation,
    String id, {
    required int page,
    required int limit,
    required String cookie,
  }) async {
    if (!const [
          'artist',
          'artistSongs',
          'artistAlbums',
          'album',
          'albumSongs',
        ].contains(operation) ||
        !RegExp(r'^[0-9]{1,32}$').hasMatch(id) ||
        page < 1 ||
        page > 100 ||
        limit < 1 ||
        limit > 1000 ||
        cookie.length > 32768 ||
        cookie.codeUnits.any((unit) => unit < 32 || unit > 126)) {
      throw ArgumentError('Invalid content arguments');
    }
    try {
      final remote = DBusRemoteObject(
        _bus,
        name: 'org.sailfishos.Mconnect.Silica',
        path: DBusObjectPath('/org/sailfishos/Mconnect/Platforms'),
      );
      final reply = await remote
          .callMethod(
            'org.sailfishos.Mconnect.Platforms',
            'NeteaseContent',
            [
              DBusString(operation),
              DBusString(id),
              DBusInt32(page),
              DBusInt32(limit),
              DBusString(cookie),
            ],
            replySignature: DBusSignature('s'),
          )
          .timeout(const Duration(seconds: 11));
      final raw = (reply.returnValues.single as DBusString).value;
      if (utf8.encode(raw).length > 4 * 1024 * 1024) {
        throw const FormatException();
      }
      final data = jsonDecode(raw);
      if (data is! Map<String, dynamic> ||
          data['backend'] != 'cpp-netease-content') {
        throw const FormatException();
      }
      if (data['error'] == 'login_required') {
        throw LoginExpiredException();
      }
      if (data['error'] != null) {
        throw ApiException(
          message: '网易云音乐资料请求失败，请稍后重试',
          code: data['error'] == 'timeout'
              ? ApiErrorCode.requestTimeout
              : ApiErrorCode.requestFailed,
        );
      }
      return NativeNeteaseContent.fromJson(data, operation);
    } on ApiException {
      rethrow;
    } on TimeoutException {
      throw ApiException(
        message: '网易云音乐资料请求超时，请稍后重试',
        code: ApiErrorCode.requestTimeout,
      );
    } catch (_) {
      throw ApiException(
        message: '网易云音乐服务暂不可用，请重新打开应用',
        code: ApiErrorCode.requestFailed,
      );
    }
  }
}

abstract interface class NativeSessionResponse {
  String? get cookie;
}

class NativeNeteaseResult implements NativeSessionResponse {
  NativeNeteaseResult({
    required this.songs,
    required this.playlists,
    this.cookie,
  });
  final List<Song> songs;
  final List<Playlist> playlists;
  @override
  final String? cookie;
  factory NativeNeteaseResult.fromJson(
    Map<String, dynamic> value, {
    required bool playlists,
    int maxRows = 50,
    PlatformType platform = PlatformType.netease,
  }) {
    String text(Map row, String key, {int limit = 8192}) {
      final value = row[key];
      if (value is! String || value.length > limit) {
        throw const FormatException();
      }
      return value;
    }

    String identity(Map row) {
      if (row['platform'] != platform.name) throw const FormatException();
      final id = text(row, 'id', limit: 256);
      if (id.isEmpty || text(row, 'name').trim().isEmpty) {
        throw const FormatException();
      }
      return id;
    }

    int number(Map row, String key, {int max = 604800000}) {
      final value = row[key];
      if (value is! int || value < 0 || value > max) {
        throw const FormatException();
      }
      return value;
    }

    int? optionalNumber(Map row, String key) {
      final value = row[key];
      if (value == null) return null;
      if (value is! int) throw const FormatException();
      return value;
    }

    String? optionalText(Map row, String key) =>
        row[key] == null ? null : text(row, key);
    String? cover(Map row) {
      final source = optionalText(row, 'coverUrl');
      if (source == null || source.isEmpty) return null;
      if (!const ['http', 'https'].contains(Uri.tryParse(source)?.scheme)) {
        throw const FormatException();
      }
      return source;
    }

    final rows = value[playlists ? 'playlists' : 'songs'];
    if (rows is! List ||
        rows.length > maxRows ||
        rows.any((row) => row is! Map)) {
      throw const FormatException();
    }
    final songs = <Song>[];
    final lists = <Playlist>[];
    for (final row in rows.cast<Map>()) {
      final id = identity(row);
      if (playlists) {
        lists.add(
          Playlist(
            id: id,
            name: text(row, 'name'),
            platform: platform,
            songCount: number(row, 'songCount', max: 10000000),
            coverUrl: cover(row),
            creatorName: optionalText(row, 'creatorName'),
            editable: false,
            collected: true,
          ),
        );
      } else {
        final rawArtists = row['artists'];
        if (rawArtists is! List ||
            rawArtists.length > 100 ||
            rawArtists.any((artist) => artist is! Map)) {
          throw const FormatException();
        }
        final artists = [
          for (final artist in rawArtists.cast<Map>())
            Artist(
              id: text(artist, 'id', limit: 256),
              name: text(artist, 'name'),
            ),
        ];
        final rawAlbum = row['album'];
        if (rawAlbum != null && rawAlbum is! Map) throw const FormatException();
        final album = rawAlbum is Map
            ? Album(
                id: text(rawAlbum, 'id', limit: 256),
                name: text(rawAlbum, 'name'),
                coverUrl: cover(rawAlbum),
              )
            : null;
        songs.add(
          Song(
            id: id,
            platform: platform,
            name: text(row, 'name'),
            artists: artists,
            album: album,
            duration: Duration(milliseconds: number(row, 'durationMs')),
            coverUrl: cover(row),
            albumId: optionalText(row, 'albumId'),
            artistId: optionalText(row, 'artistId'),
            trackNumber: optionalNumber(row, 'trackNumber'),
            fee: optionalNumber(row, 'fee'),
          ),
        );
      }
    }
    final cookie = value['cookie'];
    if (cookie != null &&
        (cookie is! String ||
            cookie.length > 32768 ||
            cookie.codeUnits.any((unit) => unit < 32 || unit > 126))) {
      throw const FormatException();
    }
    return NativeNeteaseResult(
      songs: songs,
      playlists: lists,
      cookie: cookie as String?,
    );
  }
}

class NativeNeteaseContent implements NativeSessionResponse {
  NativeNeteaseContent({
    this.artist,
    this.album,
    this.songs = const [],
    this.albums = const [],
    this.cookie,
  });
  final Artist? artist;
  final Album? album;
  final List<Song> songs;
  final List<Album> albums;
  @override
  final String? cookie;
  factory NativeNeteaseContent.fromJson(
    Map<String, dynamic> value,
    String operation, {
    PlatformType platform = PlatformType.netease,
  }) {
    Map<String, dynamic>? metadata(dynamic raw) {
      if (raw == null) {
        return null;
      }
      if (raw is! Map<String, dynamic> ||
          raw['id'] is! String ||
          (raw['id'] as String).isEmpty ||
          raw['name'] is! String ||
          (raw['name'] as String).trim().isEmpty) {
        throw const FormatException();
      }
      for (final key in [
        'id',
        'name',
        'avatarUrl',
        'coverUrl',
        'briefDesc',
        'artistId',
        'artistName',
        'releaseDate',
        'description',
        'company',
        'genre',
        'language',
      ]) {
        final field = raw[key];
        if (field != null &&
            (field is! String ||
                field.length >
                    (key == 'description' || key == 'briefDesc'
                        ? 262144
                        : key == 'id' || key == 'artistId'
                        ? 256
                        : 8192))) {
          throw const FormatException();
        }
      }
      for (final key in ['songCount', 'albumCount', 'fansCount']) {
        final field = raw[key];
        if (field != null && (field is! int || field < 0)) {
          throw const FormatException();
        }
      }
      for (final key in ['avatarUrl', 'coverUrl']) {
        final field = raw[key];
        if (field is String &&
            field.isNotEmpty &&
            !const ['http', 'https'].contains(Uri.tryParse(field)?.scheme)) {
          throw const FormatException();
        }
      }
      if (raw['releaseDate'] != null &&
          DateTime.tryParse(raw['releaseDate'] as String) == null) {
        throw const FormatException();
      }
      return raw;
    }

    final rawCookie = value['cookie'];
    if (rawCookie != null &&
        (rawCookie is! String ||
            rawCookie.length > 32768 ||
            rawCookie.codeUnits.any((unit) => unit < 32 || unit > 126))) {
      throw const FormatException();
    }
    Artist? artist;
    Album? album;
    List<Song> songs = const [];
    List<Album> albums = const [];
    if (operation == 'artist') {
      final raw = metadata(value['artist']);
      artist = raw == null ? null : Artist.fromJson(raw);
    } else if (operation == 'album') {
      final raw = metadata(value['album']);
      album = raw == null ? null : Album.fromJson(raw);
    } else if (operation == 'artistAlbums') {
      final rows = value['albums'];
      if (rows is! List || rows.length > 1000) {
        throw const FormatException();
      }
      albums = [
        for (final row in rows)
          Album.fromJson(metadata(row) ?? (throw const FormatException())),
      ];
    } else {
      songs = NativeNeteaseResult.fromJson(
        value,
        playlists: false,
        maxRows: 2000,
        platform: platform,
      ).songs;
    }
    return NativeNeteaseContent(
      artist: artist,
      album: album,
      songs: songs,
      albums: albums,
      cookie: rawCookie as String?,
    );
  }
}

/// Converts native wire models without provider decisions or network fallback.
class NativeNeteaseCatalog implements NativeSessionResponse {
  NativeNeteaseCatalog({
    this.songs = const [],
    this.playlists = const [],
    this.toplists = const [],
    this.ranked = const [],
    this.lyrics,
    this.url,
    this.qualities = const [],
    this.cookie,
  });
  final List<Song> songs;
  final List<Playlist> playlists;
  final List<Toplist> toplists;
  final List<RankedSong> ranked;
  final LyricsBundle? lyrics;
  final String? url;
  final List<AudioQuality> qualities;
  @override
  final String? cookie;
  factory NativeNeteaseCatalog.fromJson(
    Map<String, dynamic> data,
    String operation,
  ) {
    if (const ['playlist', 'daily', 'ranked', 'newSongs'].contains(operation) &&
        data['songs'] is! List) {
      throw const FormatException();
    }
    final result = NativeNeteaseResult.fromJson(
      {...data, 'songs': data['songs'] ?? <dynamic>[]},
      playlists: false,
      maxRows: 10000,
    );
    List<Playlist> playlists = const [];
    List<Toplist> toplists = const [];
    List<RankedSong> ranked = const [];
    LyricsBundle? lyrics;
    String? url;
    List<AudioQuality> qualities = const [];
    if (const [
      'playlistInfo',
      'userPlaylists',
      'toplists',
    ].contains(operation)) {
      final decoded = NativeNeteaseResult.fromJson(
        data,
        playlists: true,
        maxRows: 1000,
      ).playlists;
      playlists = [
        for (final p in decoded)
          Playlist(
            id: p.id,
            name: p.name,
            platform: p.platform,
            songCount: p.songCount,
            coverUrl: p.coverUrl,
            creatorName: p.creatorName,
            editable: operation == 'userPlaylists',
            collected: operation != 'userPlaylists',
          ),
      ];
      if (operation == 'toplists') {
        final rows = data['playlists'] as List;
        toplists = [
          for (var i = 0; i < playlists.length; i++)
            Toplist(
              id: playlists[i].id,
              name: playlists[i].name,
              coverUrl: playlists[i].coverUrl,
              songCount: playlists[i].songCount,
              updateFrequency: rows[i]['updateFrequency'] as String?,
            ),
        ];
      }
    }
    if (operation == 'ranked') {
      final ranks = data['ranks'];
      if (ranks is! List ||
          ranks.length != result.songs.length ||
          ranks.any((r) => r is! int || r < 1 || r > 20000)) {
        throw const FormatException();
      }
      for (var i = 1; i < ranks.length; i++) {
        if ((ranks[i] as int) <= (ranks[i - 1] as int)) {
          throw const FormatException();
        }
      }
      ranked = [
        for (var i = 0; i < ranks.length; i++)
          RankedSong(song: result.songs[i], rank: ranks[i] as int),
      ];
    }
    if (operation == 'lyrics') {
      final tracks = data['lyrics'];
      if (tracks is! Map<String, dynamic> ||
          tracks.values.any(
            (v) => v is! String || v.length > 2 * 1024 * 1024,
          )) {
        throw const FormatException();
      }
      final bundle = LyricsBundle(
        lrc: tracks['lrc'] as String?,
        translation: tracks['translation'] as String?,
        yrc: tracks['yrc'] as String?,
        yrcTranslation: tracks['yrcTranslation'] as String?,
        romaji: tracks['romaji'] as String?,
      );
      lyrics = bundle.isEmpty ? null : bundle;
    }
    if (operation == 'stream') {
      url = data['url'] as String;
      if (url.isEmpty ||
          !const ['https', 'http'].contains(Uri.tryParse(url)?.scheme)) {
        throw const FormatException();
      }
    }
    if (operation == 'qualities') {
      final rows = data['qualities'];
      if (rows is! List || rows.length > 8) throw const FormatException();
      qualities = [
        for (final row in rows)
          if (row is Map &&
              row['level'] is int &&
              row['level'] >= 0 &&
              row['level'] < AudioLevel.values.length &&
              row['bitrate'] is int &&
              row['bitrate'] >= 0 &&
              row['format'] is String)
            AudioQuality(
              level: AudioLevel.values[row['level'] as int],
              bitrate: row['bitrate'] as int,
              format: row['format'] as String,
            )
          else
            throw const FormatException(),
      ];
    }
    return NativeNeteaseCatalog(
      songs: result.songs,
      playlists: playlists,
      toplists: toplists,
      ranked: ranked,
      lyrics: lyrics,
      url: url,
      qualities: qualities,
      cookie: result.cookie,
    );
  }
}

class NativeNeteaseAccount implements NativeSessionResponse {
  NativeNeteaseAccount({
    required this.ok,
    this.user,
    this.vipLevel = VipLevel.free,
    this.qrKey,
    this.qrUrl,
    this.qrStatus,
    this.message,
    this.playlist,
    this.cookie,
  });
  final bool ok;
  final User? user;
  final VipLevel vipLevel;
  final String? qrKey, qrUrl, qrStatus, message;
  final Playlist? playlist;
  @override
  final String? cookie;
  factory NativeNeteaseAccount.fromJson(
    Map<String, dynamic> data,
    String operation,
  ) {
    if (data['ok'] is! bool) throw const FormatException();
    final ok = data['ok'] as bool;
    final checked = NativeNeteaseResult.fromJson({
      ...data,
      'songs': <dynamic>[],
    }, playlists: false);
    String? text(String key, int max) {
      final value = data[key];
      if (value != null && (value is! String || value.length > max)) {
        throw const FormatException();
      }
      return value as String?;
    }

    User? user;
    final raw = data['user'];
    if (raw != null) {
      if (raw is! Map<String, dynamic> ||
          raw['id'] is! String ||
          !RegExp(r'^[0-9]{1,32}$').hasMatch(raw['id'] as String) ||
          raw['nickname'] is! String ||
          (raw['nickname'] as String).length > 8192 ||
          raw['platform'] != 'netease' ||
          raw['vipLevel'] is! int ||
          raw['vipLevel'] < 0 ||
          raw['vipLevel'] > 2) {
        throw const FormatException();
      }
      final avatar = raw['avatarUrl'];
      if (avatar != null &&
          (avatar is! String ||
              avatar.length > 8192 ||
              (avatar.isNotEmpty &&
                  !const [
                    'https',
                    'http',
                  ].contains(Uri.tryParse(avatar)?.scheme)))) {
        throw const FormatException();
      }
      user = User.fromJson(raw);
    }
    final vip = data['vipLevel'] ?? 0;
    if (vip is! int || vip < 0 || vip > 2) throw const FormatException();
    final key = text('qrKey', 256),
        url = text('qrUrl', 512),
        status = text('qrStatus', 16);
    if (ok &&
        operation == 'qrKey' &&
        (key == null ||
            !RegExp(r'^[A-Za-z0-9_-]{1,256}$').hasMatch(key) ||
            url != 'https://music.163.com/login?codekey=$key')) {
      throw const FormatException();
    }
    if (operation == 'qrCheck' &&
        !const [
          'waiting',
          'scanned',
          'success',
          'expired',
          'failed',
        ].contains(status)) {
      throw const FormatException();
    }
    if (ok &&
        (operation == 'phoneLogin' ||
            (operation == 'qrCheck' && status == 'success')) &&
        user == null) {
      throw const FormatException();
    }
    Playlist? playlist;
    if (ok && operation == 'create') {
      final rows = NativeNeteaseResult.fromJson(
        data,
        playlists: true,
      ).playlists;
      if (rows.length != 1) throw const FormatException();
      final p = rows.single;
      playlist = Playlist(
        id: p.id,
        name: p.name,
        platform: p.platform,
        songCount: p.songCount,
        coverUrl: p.coverUrl,
        creatorName: p.creatorName,
        editable: true,
        collected: false,
      );
    }
    return NativeNeteaseAccount(
      ok: ok,
      user: user,
      vipLevel: VipLevel.values[vip],
      qrKey: key,
      qrUrl: url,
      qrStatus: status,
      message: text('message', 512),
      playlist: playlist,
      cookie: checked.cookie,
    );
  }
}

class NativeBilibiliResult implements NativeSessionResponse {
  NativeBilibiliResult({
    this.songs = const [],
    this.playlists = const [],
    this.qualities = const [],
    this.user,
    this.url,
    this.lyrics,
    this.qrKey,
    this.qrUrl,
    this.qrStatus,
    this.ok = true,
    this.cookie,
  });
  final List<Song> songs;
  final List<Playlist> playlists;
  final List<AudioQuality> qualities;
  final User? user;
  final String? url, lyrics, qrKey, qrUrl, qrStatus;
  final bool ok;
  @override
  final String? cookie;
  factory NativeBilibiliResult.fromJson(
    Map<String, dynamic> data,
    String operation,
  ) {
    if (const ['search', 'playlist'].contains(operation) &&
        data['songs'] is! List) {
      throw const FormatException();
    }
    final records = NativeNeteaseResult.fromJson(
      {...data, 'songs': data['songs'] ?? <dynamic>[]},
      playlists: false,
      maxRows: 10000,
      platform: PlatformType.bilibili,
    );
    String? text(String key, int maximum) {
      final value = data[key];
      if (value != null && (value is! String || value.length > maximum)) {
        throw const FormatException();
      }
      return value as String?;
    }

    List<Playlist> playlists = const [];
    if (operation == 'folders' || operation == 'share') {
      final decoded = NativeNeteaseResult.fromJson(
        data,
        playlists: true,
        maxRows: 10000,
        platform: PlatformType.bilibili,
      ).playlists;
      playlists = [
        for (final p in decoded)
          Playlist(
            id: p.id,
            name: p.name,
            platform: p.platform,
            songCount: p.songCount,
            coverUrl: p.coverUrl,
            creatorName: p.creatorName,
            editable: operation == 'folders',
            collected: false,
          ),
      ];
    }
    User? user;
    final raw = data['user'];
    if (raw != null) {
      if (raw is! Map<String, dynamic> ||
          raw['platform'] != 'bilibili' ||
          raw['id'] is! String ||
          !RegExp(r'^[0-9]{1,32}$').hasMatch(raw['id'] as String) ||
          raw['nickname'] is! String ||
          (raw['nickname'] as String).length > 8192 ||
          raw['vipLevel'] is! int ||
          raw['vipLevel'] < 0 ||
          raw['vipLevel'] > 2) {
        throw const FormatException();
      }
      final avatar = raw['avatarUrl'];
      if (avatar != null &&
          (avatar is! String ||
              avatar.length > 8192 ||
              (avatar.isNotEmpty &&
                  !const [
                    'http',
                    'https',
                  ].contains(Uri.tryParse(avatar)?.scheme)))) {
        throw const FormatException();
      }
      user = User.fromJson(raw);
    }
    final url = text('url', 16384),
        key = text('qrKey', 256),
        qrUrl = text('qrUrl', 2048),
        status = text('qrStatus', 16);
    if (operation == 'stream' &&
        (url == null ||
            !const ['http', 'https'].contains(Uri.tryParse(url)?.scheme))) {
      throw const FormatException();
    }
    if (operation == 'qrKey') {
      final uri = Uri.tryParse(qrUrl ?? '');
      if (key == null ||
          !RegExp(r'^[A-Za-z0-9_-]{1,256}$').hasMatch(key) ||
          uri?.scheme != 'https' ||
          !(uri!.host == 'bilibili.com' ||
              uri.host.endsWith('.bilibili.com'))) {
        throw const FormatException();
      }
    }
    if (operation == 'qrCheck' &&
        !const [
          'waiting',
          'scanned',
          'success',
          'expired',
          'failed',
        ].contains(status)) {
      throw const FormatException();
    }
    if ((operation == 'profile' ||
            (operation == 'qrCheck' && status == 'success')) &&
        user == null) {
      throw const FormatException();
    }
    final ok = data['ok'] ?? true;
    if (ok is! bool) throw const FormatException();
    if (const ['like', 'add', 'remove'].contains(operation) &&
        data['ok'] is! bool) {
      throw const FormatException();
    }
    List<AudioQuality> qualities = const [];
    if (operation == 'qualities') {
      final rows = data['qualities'];
      if (rows is! List || rows.length > 3) throw const FormatException();
      qualities = [
        for (final row in rows)
          if (row is Map &&
              row['level'] is int &&
              row['level'] >= 0 &&
              row['level'] <= 2 &&
              row['bitrate'] is int &&
              row['bitrate'] >= 0 &&
              row['format'] == 'm4a')
            AudioQuality(
              level: AudioLevel.values[row['level'] as int],
              bitrate: row['bitrate'] as int,
              format: 'm4a',
            )
          else
            throw const FormatException(),
      ];
    }
    return NativeBilibiliResult(
      songs: records.songs,
      playlists: playlists,
      qualities: qualities,
      user: user,
      url: url,
      lyrics: text('lyrics', 2 * 1024 * 1024),
      qrKey: key,
      qrUrl: qrUrl,
      qrStatus: status,
      ok: ok,
      cookie: records.cookie,
    );
  }
}

List<int> _decodeNativeQrImage(dynamic image) {
  if (image is! String ||
      image.length > 1400000 ||
      image.length % 4 != 0 ||
      !RegExp(r'^[A-Za-z0-9+/]*={0,2}$').hasMatch(image)) {
    throw const FormatException();
  }
  final bytes = base64Decode(image);
  final png =
      bytes.length >= 8 && base64Encode(bytes.sublist(0, 8)) == 'iVBORw0KGgo=';
  final gif =
      bytes.length >= 6 &&
      const [
        'GIF87a',
        'GIF89a',
      ].contains(ascii.decode(bytes.sublist(0, 6), allowInvalid: true));
  final jpeg =
      bytes.length >= 3 &&
      bytes[0] == 255 &&
      bytes[1] == 216 &&
      bytes[2] == 255;
  final webp =
      bytes.length >= 12 &&
      ascii.decode(bytes.sublist(0, 4), allowInvalid: true) == 'RIFF' &&
      ascii.decode(bytes.sublist(8, 12), allowInvalid: true) == 'WEBP';
  if (bytes.length > 1024 * 1024 || !(png || gif || jpeg || webp)) {
    throw const FormatException();
  }
  return bytes;
}

class NativeKugouAccount {
  NativeKugouAccount({
    this.qrKey,
    this.qrUrl,
    this.qrBytes,
    this.qrStatus,
    this.user,
    this.session,
    this.vipLevel = VipLevel.free,
  });
  final String? qrKey, qrUrl, qrStatus, session;
  final List<int>? qrBytes;
  final User? user;
  final VipLevel vipLevel;

  factory NativeKugouAccount.fromJson(
    Map<String, dynamic> data,
    String operation,
  ) {
    if (!const [
          'qrKey',
          'qrCheck',
          'qrCancel',
          'profile',
          'vip',
        ].contains(operation) ||
        data['ok'] != true) {
      throw const FormatException();
    }
    final encoded = data['session'];
    Map<String, dynamic> session = const {};
    if (operation != 'qrCancel') {
      if (encoded is! String || utf8.encode(encoded).length > 32768) {
        throw const FormatException();
      }
      final decoded = jsonDecode(encoded);
      if (decoded is! Map<String, dynamic> ||
          decoded.keys.any(
            (key) => !const [
              'token',
              'userid',
              'vip_token',
              'vip_type',
              'mid',
              'dfid',
              'uuid',
              'client',
            ].contains(key),
          )) {
        throw const FormatException();
      }
      for (final entry in decoded.entries) {
        final value = entry.value;
        if (value is! String ||
            value.length > 8192 ||
            (entry.key != 'client' &&
                value.codeUnits.any((code) => code < 32 || code > 126))) {
          throw const FormatException();
        }
      }
      if (!const ['android', 'lite'].contains(decoded['client']) ||
          (decoded['userid'] != null &&
              !RegExp(r'^[0-9]{1,18}$')
                  .hasMatch(decoded['userid'] as String))) {
        throw const FormatException();
      }
      session = decoded;
    }
    final key = data['qrKey'], status = data['qrStatus'], url = data['qrUrl'];
    if (key != null &&
        (key is! String || !RegExp(r'^[A-Za-z0-9_-]{1,64}$').hasMatch(key))) {
      throw const FormatException();
    }
    if (status != null &&
        !const [
          'waiting',
          'scanned',
          'success',
          'expired',
          'failed',
        ].contains(status)) {
      throw const FormatException();
    }
    User? user;
    final row = data['user'];
    if (row != null) {
      if (row is! Map<String, dynamic> ||
          row['platform'] != 'kugou' ||
          row['id'] is! String ||
          !RegExp(r'^[0-9]{1,18}$').hasMatch(row['id'] as String) ||
          row['id'] != session['userid'] ||
          row['nickname'] is! String ||
          (row['nickname'] as String).length > 8192 ||
          row['vipLevel'] != 0) {
        throw const FormatException();
      }
      user = User.fromJson(row);
    }
    List<int>? bytes;
    if (operation == 'qrKey') {
      final uri = url is String ? Uri.tryParse(url) : null;
      if (key == null ||
          status != null ||
          user != null ||
          uri == null ||
          uri.scheme != 'https' ||
          uri.host != 'h5.kugou.com' ||
          uri.userInfo.isNotEmpty ||
          uri.path != '/apps/loginQRCode/html/index.html' ||
          uri.queryParameters['appid'] !=
              (session['client'] == 'lite' ? '3116' : '1005') ||
          !RegExp(r'^[A-Za-z0-9_-]{1,256}$')
              .hasMatch(uri.queryParameters['qrcode'] ?? '')) {
        throw const FormatException();
      }
      if (data['qrImage'] != null) {
        bytes = _decodeNativeQrImage(data['qrImage']);
      }
    } else if (key != null || url != null || data['qrImage'] != null) {
      throw const FormatException();
    }
    if (operation == 'qrCheck' &&
        (status == null ||
            (status == 'success' &&
                (user == null ||
                    (session['token'] as String?)?.isNotEmpty != true)) ||
            (status != 'success' && user != null))) {
      throw const FormatException();
    }
    if (operation == 'profile' &&
        (user == null || (session['token'] as String?)?.isNotEmpty != true)) {
      throw const FormatException();
    }
    if (operation != 'qrCheck' && status != null) throw const FormatException();
    if ((operation == 'qrCancel' || operation == 'vip') && user != null) {
      throw const FormatException();
    }
    if (operation == 'qrCancel' && encoded != null) {
      throw const FormatException();
    }
    final level = data['vipLevel'];
    if (operation == 'vip' && (level is! int || level < 0 || level > 2)) {
      throw const FormatException();
    }
    return NativeKugouAccount(
      qrKey: key as String?,
      qrUrl: url as String?,
      qrBytes: bytes,
      qrStatus: status as String?,
      user: user,
      session: encoded as String?,
      vipLevel: operation == 'vip'
          ? VipLevel.values[level as int]
          : VipLevel.free,
    );
  }
}

class NativeKugouLibrary {
  NativeKugouLibrary({
    this.songs = const [],
    this.playlists = const [],
    this.ok = false,
  });
  final List<Song> songs;
  final List<Playlist> playlists;
  final bool ok;

  factory NativeKugouLibrary.fromJson(
    Map<String, dynamic> data,
    String operation,
  ) {
    if (!const [
      'userPlaylists',
      'playlist',
      'liked',
      'like',
      'create',
      'collect',
      'share',
    ].contains(operation)) {
      throw const FormatException();
    }
    if (const ['like', 'create', 'collect'].contains(operation) &&
        data['ok'] is! bool) {
      throw const FormatException();
    }
    List<Song> songs = const [];
    if (operation == 'playlist' || operation == 'liked') {
      songs = NativeNeteaseResult.fromJson(
        data,
        playlists: false,
        maxRows: 10000,
        platform: PlatformType.kugou,
      ).songs;
    }
    List<Playlist> playlists = const [];
    if (const ['userPlaylists', 'create', 'share'].contains(operation)) {
      final records = NativeNeteaseResult.fromJson(
        data,
        playlists: true,
        maxRows: operation == 'userPlaylists' ? 1000 : 1,
        platform: PlatformType.kugou,
      ).playlists;
      final raw = data['playlists'] as List;
      playlists = [
        for (var index = 0; index < records.length; index++)
          if (raw[index]['editable'] is bool &&
              raw[index]['collected'] is bool &&
              (raw[index]['editId'] == null ||
                  raw[index]['editId'] is String &&
                      RegExp(r'^[A-Za-z0-9_-]{1,256}$')
                          .hasMatch(raw[index]['editId'] as String)))
            Playlist(
              id: records[index].id,
              name: records[index].name,
              platform: PlatformType.kugou,
              songCount: records[index].songCount,
              coverUrl: records[index].coverUrl,
              creatorName: records[index].creatorName,
              editable: raw[index]['editable'] as bool,
              collected: raw[index]['collected'] as bool,
              editId: raw[index]['editId'] as String?,
            )
          else
            throw const FormatException(),
      ];
    }
    return NativeKugouLibrary(
      songs: songs,
      playlists: playlists,
      ok: data['ok'] == true,
    );
  }
}

class NativeQqAccount implements NativeSessionResponse {
  NativeQqAccount({
    required this.ok,
    this.qrKey,
    this.qrBytes,
    this.qrStatus,
    this.user,
    this.cookie,
  });

  final bool ok;
  final String? qrKey, qrStatus;
  final List<int>? qrBytes;
  final User? user;
  @override
  final String? cookie;

  factory NativeQqAccount.fromJson(
    Map<String, dynamic> data,
    String operation,
  ) {
    if (data['ok'] != true ||
        !const ['qrKey', 'qrCheck', 'qrCancel'].contains(operation)) {
      throw const FormatException();
    }
    final checked = NativeQqResult.fromJson(
      data,
      data['user'] == null ? 'account' : 'profile',
    );
    final key = data['qrKey'], status = data['qrStatus'];
    if (operation != 'qrCancel' && checked.cookie == null) {
      throw const FormatException();
    }
    if (key != null &&
        (key is! String || !RegExp(r'^[A-Za-z0-9_-]{1,64}$').hasMatch(key))) {
      throw const FormatException();
    }
    if (status != null &&
        !const [
          'waiting',
          'scanned',
          'success',
          'expired',
          'failed',
        ].contains(status)) {
      throw const FormatException();
    }
    List<int>? bytes;
    if (operation == 'qrKey') {
      if (status != null || checked.user != null) throw const FormatException();
      if (key == null) throw const FormatException();
      bytes = _decodeNativeQrImage(data['qrImage']);
    }
    if (operation == 'qrCheck' &&
        (status == null ||
            (status == 'success' && checked.user == null) ||
            (status != 'success' && checked.user != null))) {
      throw const FormatException();
    }
    if (operation == 'qrCancel' &&
        (key != null ||
            status != null ||
            checked.user != null ||
            data['qrImage'] != null)) {
      throw const FormatException();
    }
    return NativeQqAccount(
      ok: true,
      qrKey: key as String?,
      qrBytes: bytes,
      qrStatus: status as String?,
      user: checked.user,
      cookie: checked.cookie,
    );
  }
}

class NativeQqResult implements NativeSessionResponse {
  NativeQqResult({
    this.songs = const [],
    this.playlists = const [],
    this.qualities = const [],
    this.artist,
    this.album,
    this.albums = const [],
    this.charts = const [],
    this.ranked = const [],
    this.url,
    this.cookie,
    this.lyrics,
    this.user,
    this.vipLevel = VipLevel.free,
    this.ok = false,
    this.dirId,
    this.dailyId,
  });
  final List<Song> songs;
  final LyricsBundle? lyrics;
  final User? user;
  final VipLevel vipLevel;
  final bool ok;
  final String? dirId, dailyId;
  final List<Playlist> playlists;
  final List<AudioQuality> qualities;
  final Artist? artist;
  final Album? album;
  final List<Album> albums;
  final List<Toplist> charts;
  final List<RankedSong> ranked;
  final String? url;
  @override
  final String? cookie;
  factory NativeQqResult.fromJson(
    Map<String, dynamic> data,
    String operation, {
    PlatformType platform = PlatformType.qq,
  }) {
    if (const [
          'search',
          'artistSongs',
          'albumSongs',
          'newSongs',
          'ranked',
          'playlist',
          'liked',
        ].contains(operation) &&
        data['songs'] is! List) {
      throw const FormatException();
    }
    final records = NativeNeteaseResult.fromJson(
      {...data, 'songs': data['songs'] ?? <dynamic>[]},
      playlists: false,
      maxRows: 10000,
      platform: platform,
    );
    NativeNeteaseContent? content;
    if (const ['artist', 'artistAlbums', 'album'].contains(operation)) {
      content = NativeNeteaseContent.fromJson(
        data,
        operation,
        platform: platform,
      );
    }
    List<Playlist> playlists = const [];
    if (operation == 'searchPlaylists' ||
        operation == 'userPlaylists' ||
        operation == 'playlistInfo') {
      playlists = NativeNeteaseResult.fromJson(
        data,
        playlists: true,
        maxRows: 1000,
        platform: platform,
      ).playlists;
      if (operation == 'userPlaylists' || platform == PlatformType.kugou) {
        final raw = data['playlists'] as List;
        playlists = [
          for (var i = 0; i < playlists.length; i++)
            Playlist(
              id: playlists[i].id,
              name: playlists[i].name,
              platform: platform,
              songCount: playlists[i].songCount,
              coverUrl: playlists[i].coverUrl,
              creatorName: playlists[i].creatorName,
              editable: operation == 'userPlaylists',
              collected: operation != 'userPlaylists',
              editId: raw[i]['editId'] == null
                  ? null
                  : raw[i]['editId'] is String &&
                        RegExp(
                          platform == PlatformType.kugou
                              ? r'^[A-Za-z0-9_-]{1,256}$'
                              : r'^[0-9]{1,18}$',
                        ).hasMatch(raw[i]['editId'] as String)
                  ? raw[i]['editId'] as String
                  : throw const FormatException(),
            ),
        ];
      }
    }
    String? text(Map row, String key, [int max = 8192]) {
      final value = row[key];
      if (value != null && (value is! String || value.length > max)) {
        throw const FormatException();
      }
      return value as String?;
    }

    List<Toplist> charts = const [];
    if (operation == 'toplists') {
      final raw = data['charts'];
      if (raw is! List || raw.length > 1000) throw const FormatException();
      charts = [
        for (final row in raw)
          if (row is Map &&
              text(row, 'id')?.isNotEmpty == true &&
              text(row, 'name')?.isNotEmpty == true)
            Toplist(
              id: row['id'] as String,
              name: row['name'] as String,
              coverUrl: text(row, 'coverUrl'),
              updateFrequency: text(row, 'updateFrequency'),
              period: text(row, 'period'),
              groupName: text(row, 'groupName'),
              intro: text(row, 'intro', 262144),
              songCount: row['songCount'] == null
                  ? null
                  : row['songCount'] is int && row['songCount'] >= 0
                  ? row['songCount'] as int
                  : throw const FormatException(),
            )
          else
            throw const FormatException(),
      ];
      for (final c in charts) {
        if (c.coverUrl != null &&
            c.coverUrl!.isNotEmpty &&
            !const [
              'http',
              'https',
            ].contains(Uri.tryParse(c.coverUrl!)?.scheme)) {
          throw const FormatException();
        }
      }
    }
    List<RankedSong> ranked = const [];
    if (operation == 'ranked') {
      final raw = data['positions'];
      if (raw is! List || raw.length != records.songs.length) {
        throw const FormatException();
      }
      ranked = [
        for (var i = 0; i < raw.length; i++)
          if (raw[i] is Map &&
              raw[i]['rank'] is int &&
              raw[i]['rank'] >= 1 &&
              raw[i]['rank'] <= 20000 &&
              (raw[i]['rankChange'] == null || raw[i]['rankChange'] is int) &&
              (raw[i]['isNew'] == null || raw[i]['isNew'] is bool))
            RankedSong(
              song: records.songs[i],
              rank: raw[i]['rank'] as int,
              rankChange: raw[i]['rankChange'] as int?,
              isNew: raw[i]['isNew'] as bool?,
              rankValue: text(raw[i] as Map, 'rankValue'),
            )
          else
            throw const FormatException(),
      ];
    }
    final url = text(data, 'url', 16384);
    if (operation == 'stream' &&
        (url == null ||
            !const ['http', 'https'].contains(Uri.tryParse(url)?.scheme))) {
      throw const FormatException();
    }
    List<AudioQuality> qualities = const [];
    if (operation == 'qualities') {
      final raw = data['qualities'];
      if (raw is! List || raw.length > 8) throw const FormatException();
      qualities = [
        for (final row in raw)
          if (row is Map &&
              row['level'] is int &&
              row['level'] >= 0 &&
              row['level'] < 8 &&
              row['bitrate'] is int &&
              row['bitrate'] >= 0 &&
              const ['mp3', 'flac'].contains(row['format']))
            AudioQuality(
              level: AudioLevel.values[row['level'] as int],
              bitrate: row['bitrate'] as int,
              format: row['format'] as String,
            )
          else
            throw const FormatException(),
      ];
    }
    LyricsBundle? lyrics;
    if (operation == 'lyrics') {
      final tracks = data['lyrics'];
      final maximumLyrics = platform == PlatformType.kugou
          ? 4 * 1024 * 1024
          : 2 * 1024 * 1024;
      if (tracks is! Map<String, dynamic> ||
          tracks.keys.any(
            (k) => !const ['lrc', 'translation', 'qrc'].contains(k),
          ) ||
          tracks.values.any(
            (v) => v is! String || v.length > maximumLyrics,
          )) {
        throw const FormatException();
      }
      final bundle = LyricsBundle(
        lrc: tracks['lrc'] as String?,
        translation: tracks['translation'] as String?,
        qrc: tracks['qrc'] as String?,
      );
      lyrics = bundle.isEmpty ? null : bundle;
    }
    User? user;
    if (operation == 'profile') {
      final row = data['user'];
      if (row is! Map<String, dynamic> ||
          row['platform'] != 'qq' ||
          text(row, 'id', 18)?.isNotEmpty != true ||
          !RegExp(r'^[0-9]{1,18}$').hasMatch(row['id'] as String) ||
          text(row, 'nickname') == null ||
          row['vipLevel'] is! int ||
          row['vipLevel'] != 0) {
        throw const FormatException();
      }
      final avatar = text(row, 'avatarUrl');
      if (avatar != null &&
          avatar.isNotEmpty &&
          !const ['http', 'https'].contains(Uri.tryParse(avatar)?.scheme)) {
        throw const FormatException();
      }
      user = User.fromJson(row);
    }
    final vip = data['vipLevel'] ?? 0;
    if (operation == 'vip' &&
        (data['vipLevel'] is! int || vip < 0 || vip > 2)) {
      throw const FormatException();
    }
    if (const ['like', 'add', 'create', 'collect'].contains(operation) &&
        data['ok'] != true) {
      throw const FormatException();
    }
    final dirId = text(data, 'dirId', 18), dailyId = text(data, 'dailyId', 18);
    if ((operation == 'create' &&
            (dirId == null || !RegExp(r'^[0-9]{1,18}$').hasMatch(dirId))) ||
        (operation == 'dailyId' &&
            dailyId != null &&
            !RegExp(r'^[0-9]{1,18}$').hasMatch(dailyId))) {
      throw const FormatException();
    }
    return NativeQqResult(
      user: user,
      vipLevel: operation == 'vip'
          ? VipLevel.values[vip as int]
          : VipLevel.free,
      ok: data['ok'] == true,
      dirId: dirId,
      dailyId: dailyId,
      lyrics: lyrics,
      songs: records.songs,
      playlists: playlists,
      qualities: qualities,
      artist: content?.artist,
      album: content?.album,
      albums: content?.albums ?? const [],
      charts: charts,
      ranked: ranked,
      url: url,
      cookie: records.cookie,
    );
  }
}
