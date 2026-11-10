#include "native_lyrics.h"

#include <QHash>
#include <QRegularExpression>
#include <QTextCodec>
#include <algorithm>
#include <limits>
#include <zlib.h>

namespace {
constexpr qint64 maximumTime = std::numeric_limits<qint64>::max();
qint64 time(const QString& text) {
  bool valid = false;
  const auto value = text.toLongLong(&valid);
  return valid && value >= 0 && value <= maximumTime ? value : -1;
}
QVariantMap line(qint64 at, const QString& text, const QVariantList& words) {
  return {{"time", at}, {"text", text}, {"translation", ""}, {"words", words}};
}
QString sniff(const QString& content) {
  for (const auto& format : QStringList{"qrc", "krc", "lrc"}) if (!ParseNativeLyrics(content, format).isEmpty()) return format;
  return "lrc";
}
QString decryptKrc(QByteArray bytes) {
  if (!bytes.startsWith("krc1")) {
    auto encoded = bytes.trimmed();
    encoded.replace('-', '+'); encoded.replace('_', '/');
    if (encoded.size() % 4) encoded.append(QByteArray(4 - encoded.size() % 4, '='));
    bytes = QByteArray::fromBase64(encoded);
  }
  if (bytes.size() <= 4 || bytes.size() > 4 * 1024 * 1024) return {};
  bytes.remove(0, 4);
  const unsigned char key[] = {0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47, 0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69};
  for (int index = 0; index < bytes.size(); ++index) bytes[index] = char(static_cast<unsigned char>(bytes[index]) ^ key[index % 16]);
  QByteArray result(2 * 1024 * 1024, '\0');
  uLongf length = result.size();
  if (uncompress(reinterpret_cast<Bytef*>(result.data()), &length, reinterpret_cast<const Bytef*>(bytes.constData()), bytes.size()) != Z_OK) return {};
  result.resize(int(length));
  return QString::fromUtf8(result);
}
}

QVariantList ParseNativeLyrics(const QString& content, const QString& format) {
  if (format != "qrc" && format != "krc" && format != "yrc") return ParseNativeLrc(content);
  if (content.size() > 2 * 1024 * 1024) return {};
  QVariantList lines;
  if (format == "qrc") {
    const QRegularExpression row("<L\\s+T=\"([0-9]+)\"\\s+D=\"([0-9]+)\">(.*?)</L>");
    const QRegularExpression word("<P\\s+T=\"([0-9]+)\"\\s+D=\"([0-9]+)\">(.*?)</P>");
    auto rows = row.globalMatch(content);
    while (rows.hasNext()) {
      const auto match = rows.next();
      const auto start = time(match.captured(1));
      if (start < 0) continue;
      const auto body = match.captured(3);
      QVariantList words;
      auto chunks = word.globalMatch(body);
      while (chunks.hasNext()) {
        const auto chunk = chunks.next();
        const auto at = time(chunk.captured(1)), duration = time(chunk.captured(2));
        if (at < 0 || duration < 0 || at > maximumTime - start) continue;
        words.append(QVariantMap{{"word", chunk.captured(3)}, {"start", start + at}, {"duration", duration}});
      }
      auto text = body;
      text.remove(QRegularExpression("<[^>]+>"));
      lines.append(line(start, text, words));
      if (lines.size() > 20000) return {};
    }
  } else {
    const QRegularExpression row(format == "yrc" ? "^\\[([0-9]+),([0-9]+)\\]" : "\\[([0-9]+),([0-9]+)\\]");
    const QRegularExpression word(format == "yrc" ? "\\(([0-9]+),([0-9]+),([0-9]+)\\)([^(]*)" : "<([0-9]+),([0-9]+),[0-9]+>([^<]+)");
    for (const auto& raw : content.split('\n')) {
      const auto match = row.match(raw);
      if (!match.hasMatch()) continue;
      const auto start = time(match.captured(1));
      if (start < 0) continue;
      const auto body = raw.mid(match.capturedEnd());
      QVariantList words;
      QString text;
      qint64 current = start;
      auto chunks = word.globalMatch(body);
      while (chunks.hasNext()) {
        const auto chunk = chunks.next();
        const auto duration = time(chunk.captured(2));
        const auto at = format == "yrc" ? time(chunk.captured(1)) : current - start;
        const auto value = chunk.captured(format == "yrc" ? 4 : 3);
        if (at < 0 || duration < 0 || at > maximumTime - start || value.isEmpty()) continue;
        words.append(QVariantMap{{"word", value}, {"start", start + at}, {"duration", duration}});
        text += value;
        if (format == "krc") current = duration > maximumTime - current ? maximumTime : current + duration;
      }
      if (format == "krc") { text = body; text.remove(QRegularExpression("<[^>]+>")); }
      text = text.trimmed();
      if (text.isEmpty()) continue;
      lines.append(line(start, text, words));
      if (lines.size() > 20000) return {};
    }
  }
  std::stable_sort(lines.begin(), lines.end(), [](const QVariant& a, const QVariant& b) { return a.toMap().value("time").toLongLong() < b.toMap().value("time").toLongLong(); });
  return lines;
}

QVariantMap BuildNativeLyricsBundle(const QVariantMap& bundle) {
  QString content, format;
  for (const auto& candidate : QStringList{"yrc", "qrc"}) {
    const auto text = bundle.value(candidate).toString();
    if (!text.trimmed().isEmpty() && !ParseNativeLyrics(text, candidate).isEmpty()) { content = text; format = candidate; break; }
  }
  if (content.isEmpty()) { content = bundle.value("lrc").toString(); format = sniff(content); }
  auto lines = ParseNativeLyrics(content, format);
  const auto translations = ParseNativeLrc(bundle.value(format == "yrc" ? "yrcTranslation" : "translation").toString());
  QHash<qint64, QString> byTime;
  for (const auto& raw : translations) {
    const auto row = raw.toMap();
    const auto at = row.value("time").toLongLong();
    if (!byTime.contains(at) && !row.value("text").toString().trimmed().isEmpty()) byTime[at] = row.value("text").toString().trimmed();
  }
  for (auto& raw : lines) {
    auto row = raw.toMap();
    if (row.value("translation").toString().trimmed().isEmpty()) row["translation"] = byTime.value(row.value("time").toLongLong());
    raw = row;
  }
  return {{"lines", lines}, {"format", format}, {"content", content}};
}

QString DecodeNativeLyricBytes(QByteArray bytes) {
  if (bytes.startsWith(QByteArray::fromHex("efbbbf"))) bytes.remove(0, 3);
  const auto text = QString::fromUtf8(bytes);
  if (text.toUtf8() == bytes) return text;
  if (auto* codec = QTextCodec::codecForName("GB18030")) {
    QTextCodec::ConverterState state;
    const auto decoded = codec->toUnicode(bytes.constData(), bytes.size(), &state);
    if (state.invalidChars == 0) return decoded;
  }
  return text;
}

QVariantMap DecodeNativeSidecar(const QByteArray& bytes, const QString& extension) {
  const auto name = extension.toLower().startsWith('.') ? extension.mid(1).toLower() : extension.toLower();
  if (!QStringList{"lrc", "txt", "qrc", "krc"}.contains(name)) return {};
  const auto content = name == "krc" ? decryptKrc(bytes) : DecodeNativeLyricBytes(bytes);
  if (content.trimmed().isEmpty()) return {};
  const auto format = name == "txt" ? QString("lrc") : name;
  if ((format == "krc" || format == "qrc") && ParseNativeLyrics(content, format).isEmpty()) return {};
  return {{"content", content}, {"format", format}};
}
