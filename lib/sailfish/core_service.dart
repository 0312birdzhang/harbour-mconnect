import 'dart:convert';

import 'package:dbus/dbus.dart';
import 'core_transfer.dart';

Map<String, dynamic> decodeCoreArguments(String method, String raw) {
  final limit = method == 'importPlaylist' ? 262144 : 65536;
  if (raw.length > limit) throw ArgumentError();
  final value = jsonDecode(raw);
  if (value is! Map<String, dynamic>) throw ArgumentError();
  return value;
}

class CoreRequestFailure implements Exception {
  const CoreRequestFailure(this.message);
  final String message;
}

/// Versioned presentation boundary. QML never receives cookies or audio URLs.
class SailfishCoreService extends DBusObject {
  SailfishCoreService({required this.dispatch, DBusClient? bus})
    : _bus = bus ?? DBusClient.session(),
      super(DBusObjectPath('/org/sailfishos/Mconnect/Core'));

  static const service = 'org.sailfishos.Mconnect.Core';
  static const interface = service;
  final Future<Object?> Function(String method, Map<String, dynamic> args)
  dispatch;
  final DBusClient _bus;
  final _transfers = CoreTransferStore();

  Future<void> start() async {
    await _bus.registerObject(this);
    await _bus.requestName(service);
  }

  Future<void> close() => _bus.close();

  Future<void> publish(String topic, Object? value) {
    final json = jsonEncode(value);
    final body = utf8.encode(json).length > 48 * 1024
        ? '{"_mconnectRefresh":true}'
        : json;
    return emitSignal(interface, 'Changed', [
      DBusString(topic),
      DBusString(body),
    ]);
  }

  @override
  List<DBusIntrospectInterface> introspect() => [
    DBusIntrospectInterface(
      interface,
      methods: [
        DBusIntrospectMethod(
          'Request',
          args: [
            DBusIntrospectArgument(
              DBusSignature('s'),
              DBusArgumentDirection.in_,
            ),
            DBusIntrospectArgument(
              DBusSignature('s'),
              DBusArgumentDirection.in_,
            ),
            DBusIntrospectArgument(
              DBusSignature('s'),
              DBusArgumentDirection.out,
            ),
          ],
        ),
      ],
      signals: [
        DBusIntrospectSignal(
          'Changed',
          args: [
            DBusIntrospectArgument(
              DBusSignature('s'),
              DBusArgumentDirection.out,
            ),
            DBusIntrospectArgument(
              DBusSignature('s'),
              DBusArgumentDirection.out,
            ),
          ],
        ),
      ],
    ),
  ];

  @override
  Future<DBusMethodResponse> handleMethodCall(DBusMethodCall methodCall) async {
    if (methodCall.interface != interface) {
      return DBusMethodErrorResponse.unknownInterface();
    }
    if (methodCall.name != 'Request') {
      return DBusMethodErrorResponse.unknownMethod();
    }
    if (methodCall.signature != DBusSignature('ss')) {
      return DBusMethodErrorResponse.invalidArgs();
    }
    try {
      final method = methodCall.values[0].asString();
      final args = decodeCoreArguments(method, methodCall.values[1].asString());
      if (method == '_readTransfer') {
        return DBusMethodSuccessResponse([
          DBusString(jsonEncode(_transfers.part(args))),
        ]);
      }
      final value = await dispatch(method, args);
      return DBusMethodSuccessResponse([DBusString(_transfers.encode(value))]);
    } on CoreRequestFailure catch (error) {
      return DBusMethodErrorResponse.failed(error.message);
    } on ArgumentError {
      return DBusMethodErrorResponse.invalidArgs();
    } on FormatException {
      return DBusMethodErrorResponse.invalidArgs();
    } catch (_) {
      // Provider errors may contain signed URLs or session data.
      return DBusMethodErrorResponse.failed('请求失败，请稍后重试');
    }
  }
}
