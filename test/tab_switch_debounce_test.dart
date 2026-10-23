import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:go_router/go_router.dart';
import 'package:mconnect/core/router/app_router.dart';
import 'package:mconnect/core/widgets/app_bottom_nav_bar.dart';
import 'package:mconnect/features/player/presentation/providers/player_provider.dart';
import 'package:mconnect/models/platform_type.dart';

import 'support/content_page_fakes.dart';

/// Tab-switch debounce on the path that actually runs.
///
/// The 80 ms debounce used to live in `HomeScreen`, which never executes it while
/// a `ShellRoute` exists (the shell owns the nav capsule and `HomeScreen` reports
/// `ownsBottomLayer = false`), so every tap issued its own `go()` + `setState`.
void main() {
  testWidgets(
    'library child keeps navigation and current tab returns to root',
    (tester) async {
      final platform = registerFake(
        FakeContentPlatform(type: PlatformType.netease),
      );
      final player = PlayerNotifier(
        audioController: IdleAudioController(),
        platformResolver: (_) => platform,
      );
      final router = GoRouter(
        initialLocation: '/?tab=2',
        routes: [
          ShellRoute(
            builder: (context, state, child) =>
                AppRouteShell(path: state.uri.path, child: child),
            routes: [
              GoRoute(path: '/', builder: (_, _) => const Text('library root')),
              GoRoute(
                path: '/likes',
                builder: (_, _) => const Text('likes child'),
              ),
            ],
          ),
        ],
      );
      addTearDown(router.dispose);
      await tester.pumpWidget(
        ProviderScope(
          overrides: [playerProvider.overrideWith((ref) => player)],
          child: MaterialApp.router(routerConfig: router),
        ),
      );
      await tester.pumpAndSettle();
    unawaited(router.push<void>('/likes'));
      await tester.pumpAndSettle();
      expect(find.text('likes child'), findsOneWidget);
      final nav = tester.widget<AppBottomNavBar>(find.byType(AppBottomNavBar));
      expect(nav.selectedIndex, 2);
      nav.onDestinationSelected(2);
      await tester.pumpAndSettle();
      expect(router.routeInformationProvider.value.uri.toString(), '/?tab=2');
      expect(find.text('library root'), findsOneWidget);
      expect(find.text('likes child'), findsNothing);
      expect(find.byType(AppBottomNavBar), findsOneWidget);
      await tester.pumpWidget(const SizedBox.shrink());
    },
  );

  group('TabSwitchThrottle（可注入时钟）', () {
    test('一串连点只放行第一次，其余全部被丢', () {
      var now = DateTime(2026, 1, 1, 12);
      final throttle = TabSwitchThrottle(clock: () => now);

      var accepted = 0;
      for (var i = 0; i < 10; i++) {
        if (throttle.shouldAccept(1, 0)) accepted++;
      }

      expect(accepted, 1, reason: '同一帧里的 10 次连点只允许切换一次');
      expect(throttle.skipped, 9);
    });

    test('窗口过后重新放行（不是把 tab 焊死）', () {
      var now = DateTime(2026, 1, 1, 12);
      final throttle = TabSwitchThrottle(clock: () => now);

      expect(throttle.shouldAccept(1, 0), isTrue);
      now = now.add(tabSwitchDebounce + const Duration(milliseconds: 1));
      expect(throttle.shouldAccept(1, 0), isTrue);
    });

    test('点当前 tab 永远不触发切换', () {
      var now = DateTime(2026, 1, 1, 12);
      final throttle = TabSwitchThrottle(clock: () => now);

      expect(throttle.shouldAccept(2, 2), isFalse);
      expect(throttle.skipped, 1);

      // …and it must not consume the window for a real switch right after.
      expect(throttle.shouldAccept(3, 2), isTrue);
    });
  });

  testWidgets('连点两个 tab：同一帧内的第二次点击被防抖丢弃', (tester) async {
    final platform = registerFake(
      FakeContentPlatform(type: PlatformType.netease),
    );
    final player = PlayerNotifier(
      audioController: IdleAudioController(),
      platformResolver: (_) => platform,
    );

    // A minimal shell: the real `AppRouteShell` + real `AppBottomNavBar`, so the
    // assertion is about the production `_onTabSelected` path and not a copy.
    final router = GoRouter(
      initialLocation: '/',
      routes: [
        ShellRoute(
          pageBuilder: (context, state, child) => MaterialPage<void>(
            child: AppRouteShell(path: state.uri.path, child: child),
          ),
          routes: [
            GoRoute(
              path: '/',
              pageBuilder: (context, state) =>
                  const MaterialPage<void>(child: Text('home')),
            ),
            GoRoute(
              path: '/likes',
              pageBuilder: (context, state) =>
                  const MaterialPage<void>(child: Text('likes')),
            ),
          ],
        ),
      ],
    );

    await tester.pumpWidget(
      ProviderScope(
        overrides: [playerProvider.overrideWith((ref) => player)],
        child: MaterialApp.router(routerConfig: router),
      ),
    );
    await tester.pumpAndSettle();

    AppBottomNavBar navBar() =>
        tester.widget<AppBottomNavBar>(find.byType(AppBottomNavBar));
    expect(navBar().selectedIndex, 0);

    // Dispatch both selections synchronously. Awaiting tester.tap can exceed
    // the real-clock 80 ms window while ARM builds compete for host CPU.
    final select = navBar().onDestinationSelected;
    select(1);
    select(2);
    await tester.pumpAndSettle();

    expect(navBar().selectedIndex, 1, reason: '第二次点击（tab 2）落在 80ms 窗口内，必须被丢弃');
  });
}
