#include "../elinux/silica/native_lyrics.h"

#include <QCoreApplication>
#include <QTextCodec>
#include <zlib.h>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "LYRIC_DOCUMENT_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  const auto yrc = QStringLiteral("[20370,400](0,200,0)第一)(200,200,0)段");
  const auto rows = ParseNativeLyrics(yrc, "yrc");
  CHECK(rows.size() == 1 && rows.first().toMap().value("text") == QStringLiteral("第一)段"));
  const auto words = rows.first().toMap().value("words").toList();
  CHECK(words.size() == 2 && words.last().toMap().value("start") == 20570);
  CHECK(ParseNativeLyrics("[604800001,1](0,1,0)beyond-week", "yrc").size() == 1);
  CHECK(ParseNativeLyrics("[9223372036854775806,4](3,1,0)overflow", "yrc").isEmpty());
  const auto translated = BuildNativeLyricsBundle({{"yrc", yrc}, {"lrc", "[00:20.316]plain"},
      {"translation", "[00:20.316]wrong-timeline"}, {"yrcTranslation", "[00:20.370]correct-timeline"}});
  CHECK(translated.value("format") == "yrc");
  CHECK(translated.value("lines").toList().first().toMap().value("translation") == "correct-timeline");
  const auto fallback = BuildNativeLyricsBundle({{"yrc", "broken"}, {"qrc", "broken"}, {"lrc", "[00:01.00]fallback"}});
  CHECK(fallback.value("format") == "lrc" && fallback.value("lines").toList().size() == 1);
  const auto qrc = ParseNativeLyrics("<L T=\"1500\" D=\"800\"><P T=\"100\" D=\"300\">QQ 词</P></L>", "qrc");
  CHECK(qrc.first().toMap().value("words").toList().first().toMap().value("start") == 1600);
  CHECK(DecodeNativeSidecar("[00:01.00]fake", ".qrc").isEmpty());
  CHECK(!DecodeNativeSidecar("untimed text", ".txt").isEmpty());
  CHECK(DecodeNativeSidecar("!!!invalid!!!", ".krc").isEmpty());
  const auto plain = QStringLiteral("[1000,400]<0,200,0>词<200,200,0>第二段\n");
  const auto utf8 = plain.toUtf8();
  QByteArray compressed(int(compressBound(utf8.size())), '\0');
  uLongf length = compressed.size();
  CHECK(compress(reinterpret_cast<Bytef*>(compressed.data()), &length, reinterpret_cast<const Bytef*>(utf8.constData()), utf8.size()) == Z_OK);
  compressed.resize(int(length));
  const unsigned char key[] = {0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47, 0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69};
  for (int index = 0; index < compressed.size(); ++index) compressed[index] = char(static_cast<unsigned char>(compressed[index]) ^ key[index % 16]);
  const auto payload = QByteArray("krc1") + compressed;
  CHECK(DecodeNativeSidecar(payload, "krc").value("content") == plain);
  CHECK(DecodeNativeSidecar((QByteArray(4, '\0') + compressed).toBase64(), ".krc").value("content") == plain);
  const auto krc = BuildNativeLyricsBundle({{"lrc", plain}});
  CHECK(krc.value("format") == "krc" && krc.value("lines").toList().size() == 1);
  CHECK(krc.value("lines").toList().first().toMap().value("words").toList().last().toMap().value("start") == 1200);
  CHECK(DecodeNativeLyricBytes(QByteArray("[00:01.00]") + QByteArray::fromHex("d6d0cec4b8e8b4ca")) == QStringLiteral("[00:01.00]中文歌词"));
  CHECK(DecodeNativeLyricBytes(QByteArray::fromHex("efbbbf") + utf8) == plain);
  CHECK(ParseNativeLrc("[00:01.00]x\n[offset:2147483648]").first().toMap().value("time").toLongLong() == 2147484648LL);
  puts("NATIVE_LYRIC_DOCUMENT_PASS yrc-parentheses exact-translations format-fallback qrc-timing krc-roundtrip gb18030 bom large-offsets");
  return 0;
}
