import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/core/theme/app_theme.dart';
import 'package:mconnect/core/theme/miuix_theme.dart';
import 'package:mconnect/features/player/presentation/widgets/word_by_word_lyrics.dart';
import 'package:mconnect/lyrics/models/lyrics_line.dart';

void main() {
  test('titles and body text keep CJK fallback in both themes and brightnesses', () {
    for (final theme in [AppTheme.light(), AppTheme.dark(),
      miuixTheme(brightness: Brightness.light, seedColor: AppTheme.defaultSeedColor),
      miuixTheme(brightness: Brightness.dark, seedColor: AppTheme.defaultSeedColor)]) {
      for (final style in [theme.appBarTheme.titleTextStyle, theme.textTheme.titleLarge,
        theme.textTheme.titleMedium, theme.textTheme.bodyMedium]) {
        expect(style!.fontFamilyFallback, contains('SailfishCJK'));
        expect(style.fontFamilyFallback, contains('SailfishEmoji'));
      }
    }
  });
  testWidgets('highlighted word lyrics preserve the system font fallback', (tester) async {
    await tester.pumpWidget(MaterialApp(
      theme: AppTheme.light(),
      home: const Scaffold(body: WordByWordLine(
        line: LyricsLine(timestamp: Duration.zero, text: '中文歌词'),
        currentPosition: Duration(seconds: 1), isCurrentLine: true,
        playedCharacters: 1,
      )),
    ));
    final text = tester.widget<RichText>(find.byType(RichText).first);
    final style = text.text.style!;
    expect(style.fontFamily, AppTheme.systemFont);
    expect(style.fontFamilyFallback, AppTheme.systemFontFallback);
    expect(style.fontWeight, FontWeight.bold);
  });
  testWidgets('colour spans never split surrogate pairs or joined emoji', (tester) async {
    const value = '甲𠮷👩‍🎤乙';
    for (var offset = 1; offset < value.length; offset++) {
      await tester.pumpWidget(const MaterialApp(home: Scaffold(body: SizedBox())));
      await tester.pumpWidget(MaterialApp(home: Scaffold(body: PlayedLyricsText(
        text: value, playedCharacters: offset,
        playedColor: Colors.red, baseColor: Colors.grey,
      ))));
      final text = tester.widget<Text>(find.byType(Text).first);
      final span = text.textSpan as TextSpan;
      final prefix = (span.children!.first as TextSpan).text!;
      expect(['甲', '甲𠮷', '甲𠮷👩‍🎤'], contains(prefix));
      expect(span.toPlainText(), value);
    }
  });
}
