import 'dart:async';
import 'dart:io';

enum ConnectivityResult { none, wifi, ethernet, mobile, vpn, other, bluetooth }

/// Read the kernel's default routes: an associated Wi-Fi interface alone must
/// not authorize downloads when mobile data owns the default route.
class Connectivity {
  Connectivity({
    this.routePath = '/proc/net/route',
    this.ipv6RoutePath = '/proc/net/ipv6_route',
    this.networkRoot = '/sys/class/net',
    this.probe,
  });
  final String routePath;
  final String ipv6RoutePath;
  final String networkRoot;
  final Future<List<ConnectivityResult>> Function()? probe;

  Future<List<ConnectivityResult>> checkConnectivity() async {
    if (probe != null) return probe!();
    try {
      return await _readRoutes();
    } on FileSystemException {
      return [ConnectivityResult.other];
    }
  }

  Future<List<ConnectivityResult>> _readRoutes() async {
    final file = File(routePath);
    if (!await file.exists()) return [ConnectivityResult.other];
    final routes = await file.readAsLines();
    final defaults = <(String, int)>[];
    for (final line in routes.skip(1)) {
      final fields = line.trim().split(RegExp(r'\s+'));
      if (fields.length < 8 || fields[1] != '00000000') continue;
      final flags = int.tryParse(fields[3], radix: 16) ?? 0;
      if (flags & 1 == 0) continue;
      defaults.add((fields[0], int.tryParse(fields[6]) ?? 0));
    }
    // IPv6-only mobile networks have no IPv4 default route.
    if (defaults.isEmpty && await File(ipv6RoutePath).exists()) {
      for (final line in await File(ipv6RoutePath).readAsLines()) {
        final fields = line.trim().split(RegExp(r'\s+'));
        if (fields.length < 10 ||
            fields[0] != '0' * 32 ||
            fields[1] != '00' ||
            fields[9] == 'lo') {
          continue;
        }
        final flags = int.tryParse(fields[8], radix: 16) ?? 0;
        if ((flags & 1) == 0 || (flags & 0x200) != 0) continue;
        defaults.add((fields[9], int.tryParse(fields[5], radix: 16) ?? 0));
      }
    }
    if (defaults.isEmpty) return [ConnectivityResult.none];
    defaults.sort((a, b) => a.$2.compareTo(b.$2));
    final name = defaults.first.$1;
    if (await Directory('$networkRoot/$name/wireless').exists()) {
      return [ConnectivityResult.wifi];
    }
    if (name.startsWith('eth') || name.startsWith('en')) {
      return [ConnectivityResult.ethernet];
    }
    if (name.startsWith('rmnet') ||
        name.startsWith('wwan') ||
        name.startsWith('pdp')) {
      return [ConnectivityResult.mobile];
    }
    // VPN/unknown must not bypass the Wi-Fi-only policy.
    return [ConnectivityResult.other];
  }

  Stream<List<ConnectivityResult>> get onConnectivityChanged {
    String? previous;
    Timer? timer;
    var reading = false;
    late final StreamController<List<ConnectivityResult>> controller;
    Future<void> poll() async {
      if (reading || !controller.hasListener) return;
      reading = true;
      try {
        final current = await checkConnectivity();
        if (!controller.hasListener || current.toString() == previous) return;
        previous = current.toString();
        controller.add(current);
      } finally {
        reading = false;
      }
    }

    controller = StreamController<List<ConnectivityResult>>(
      onListen: () {
        timer = Timer.periodic(
          const Duration(seconds: 5),
          (_) => unawaited(poll()),
        );
        unawaited(poll());
      },
      onCancel: () {
        timer?.cancel();
      },
    );
    return controller.stream;
  }
}
