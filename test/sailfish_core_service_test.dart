import 'dart:convert';
import 'dart:io';
import 'package:dbus/dbus.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/sailfish/core_service.dart';

void main() {
  test(
    'versioned bridge dispatches on isolated bus and rejects malformed requests',
    () async {
      final directory = await Directory.systemTemp.createTemp('mconnect-core-');
      final server = DBusServer();
      final address = await server.listenAddress(
        DBusAddress.unix(dir: directory),
      );
      final client = DBusClient(address);
      final seen = <String>[];
      final service = SailfishCoreService(
        bus: DBusClient(address),
        dispatch: (method, args) async {
          seen.add(method);
          if (method == 'large') return {'text': '中文🎵' * 30000};
          if (method == 'throw') throw Exception('secret-cookie');
          return {'version': 1, 'query': args['query']};
        },
      );
      addTearDown(() async {
        await client.close();
        await service.close();
        await server.close();
        await directory.delete(recursive: true);
      });
      await service.start();
      final object = DBusRemoteObject(
        client,
        name: SailfishCoreService.service,
        path: DBusObjectPath('/org/sailfishos/Mconnect/Core'),
      );
      final reply = await object.callMethod(
        SailfishCoreService.interface,
        'Request',
        [
          const DBusString('search'),
          DBusString(jsonEncode({'query': '中文🎵'})),
        ],
      );
      expect(jsonDecode(reply.values.single.asString()), {
        'version': 1,
        'query': '中文🎵',
      });
      for (final bad in ['[]', '{', 'null', 'x' * 65537]) {
        await expectLater(
          object.callMethod(SailfishCoreService.interface, 'Request', [
            const DBusString('search'),
            DBusString(bad),
          ]),
          throwsA(isA<DBusMethodResponseException>()),
        );
      }
      expect(seen, ['search']);
      final imported = await object.callMethod(
        SailfishCoreService.interface,
        'Request',
        [
          const DBusString('importPlaylist'),
          DBusString(jsonEncode({'link': 'x' * 70000})),
        ],
      );
      expect(jsonDecode(imported.values.single.asString())['version'], 1);
      final large = await object.callMethod(
        SailfishCoreService.interface,
        'Request',
        [const DBusString('large'), const DBusString('{}')],
      );
      final descriptor = jsonDecode(large.values.single.asString());
      final chunks = <int>[];
      for (var i = 0; i < descriptor['parts']; i++) {
        final part = await object.callMethod(
          SailfishCoreService.interface,
          'Request',
          [
            const DBusString('_readTransfer'),
            DBusString(
              jsonEncode({
                'token': descriptor['_mconnectTransfer'],
                'index': i,
              }),
            ),
          ],
        );
        chunks.addAll(
          base64Decode(
            jsonDecode(part.values.single.asString())['data'] as String,
          ),
        );
      }
      expect(jsonDecode(utf8.decode(chunks))['text'], '中文🎵' * 30000);
      expect(
        () => decodeCoreArguments(
          'importPlaylist',
          jsonEncode({'link': 'x' * 262144}),
        ),
        throwsArgumentError,
      );
      try {
        await object.callMethod(SailfishCoreService.interface, 'Request', [
          const DBusString('throw'),
          const DBusString('{}'),
        ]);
        fail('Expected sanitized failure');
      } on DBusMethodResponseException catch (e) {
        expect(e.toString(), isNot(contains('secret-cookie')));
      }
      final signal = DBusSignalStream(
        client,
        interface: SailfishCoreService.interface,
        name: 'Changed',
      ).first;
      // Ensure the match rule is installed before publishing.
      await client.callMethod(
        destination: 'org.freedesktop.DBus',
        path: DBusObjectPath('/org/freedesktop/DBus'),
        interface: 'org.freedesktop.DBus',
        name: 'GetId',
        values: [],
      );
      await service.publish('state', {'playing': false});
      expect((await signal).values.map((v) => v.asString()).toList(), [
        'state',
        '{"playing":false}',
      ]);
    },
  );
}
