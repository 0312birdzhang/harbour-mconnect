import 'dart:io';
import 'dart:async';
import 'package:flutter_test/flutter_test.dart';
import 'package:fake_async/fake_async.dart';
import 'package:mconnect/sailfish/connectivity.dart';

void main() {
  test(
    'Wi-Fi cache policy follows the preferred default route including IPv6',
    () async {
      final root = await Directory.systemTemp.createTemp('mconnect-routes-');
      addTearDown(() => root.delete(recursive: true));
      final route = File('${root.path}/route');
      final ipv6 = File('${root.path}/ipv6');
      await Directory(
        '${root.path}/net/wlan0/wireless',
      ).create(recursive: true);
      final monitor = Connectivity(
        routePath: route.path,
        ipv6RoutePath: ipv6.path,
        networkRoot: '${root.path}/net',
      );
      const header = 'Iface Destination Gateway Flags RefCnt Use Metric Mask\n';
      await route.writeAsString(
        '${header}wlan0 00000000 00000000 0001 0 0 10 00000000\n',
      );
      expect(await monitor.checkConnectivity(), [ConnectivityResult.wifi]);
      await route.writeAsString(
        '${header}wlan0 00000000 00000000 0001 0 0 10 00000000\n'
        'rmnet0 00000000 00000000 0001 0 0 5 00000000\n',
      );
      expect(await monitor.checkConnectivity(), [ConnectivityResult.mobile]);
      await route.writeAsString(
        '${header}tun0 00000000 00000000 0001 0 0 1 00000000\n',
      );
      expect(await monitor.checkConnectivity(), [ConnectivityResult.other]);
      await route.writeAsString(header);
      await ipv6.writeAsString(
        '${'0' * 32} 00 ${'0' * 32} 00 ${'0' * 32} 00000010 00000000 00000000 00000001 wlan0\n',
      );
      expect(await monitor.checkConnectivity(), [ConnectivityResult.wifi]);
      await ipv6.writeAsString('');
      expect(await monitor.checkConnectivity(), [ConnectivityResult.none]);
    },
  );

  test(
    'cancelling connection monitoring immediately releases its timer',
    () async {
      late Future<void> cancelled;
      fakeAsync((clock) {
        final pending = Completer<List<ConnectivityResult>>();
        final subscription = Connectivity(probe: () => pending.future)
            .onConnectivityChanged.listen((_) {});
        cancelled = subscription.cancel();
        clock.flushMicrotasks();
        expect(clock.periodicTimerCount, 0);
        pending.complete([ConnectivityResult.none]);
        clock.flushMicrotasks();
      });
      await cancelled;
    },
  );
}
