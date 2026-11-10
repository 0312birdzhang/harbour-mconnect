#include "native_lyrics.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <sqlite3.h>
#include <algorithm>
#include <limits>
#include <vector>

QVariantList ParseNativeLrc(const QString& content) {
  if (content.size() > 2 * 1024 * 1024) return {};
  const QRegularExpression time(QStringLiteral("\\[(\\d{2}):(\\d{2})\\.(\\d{2,3})\\]"));
  const QRegularExpression tags(QStringLiteral("\\[(ti|ar|al|by|offset|length):([^\\]]*)\\]"), QRegularExpression::CaseInsensitiveOption);
  qint64 offset = 0;
  auto tag = tags.globalMatch(content);
  while (tag.hasNext()) {
    const auto m = tag.next();
    if (m.captured(1).compare("offset", Qt::CaseInsensitive) == 0) {
      bool ok = false;
      const qint64 value = m.captured(2).trimmed().toLongLong(&ok);
      if (ok) offset = value;
    }
  }
  struct Line { qint64 time; QString text; };
  std::vector<Line> lines;
  for (const auto& raw : content.split('\n')) {
    QString text = raw;
    text.remove(time); text.remove(tags); text = text.trimmed();
    if (text.isEmpty()) continue;
    auto matches = time.globalMatch(raw);
    while (matches.hasNext()) {
      const auto m = matches.next();
      const auto fraction = m.captured(3);
      const qint64 base = m.captured(1).toLongLong()*60000 + m.captured(2).toLongLong()*1000
          + fraction.toInt()*(fraction.size()==2 ? 10 : 1);
      const qint64 ms = offset > std::numeric_limits<qint64>::max() - base ? std::numeric_limits<qint64>::max() : base + offset;
      lines.push_back({std::max(qint64(0), ms), text});
      if (lines.size() > 20000) return {};
    }
  }
  std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b){return a.time < b.time;});
  QVariantList result;
  for (size_t i=0; i<lines.size();) {
    const auto first = lines[i++];
    QSet<QString> seen{first.text};
    QString translation;
    QVariantList extras;
    while (i<lines.size() && lines[i].time==first.time) {
      const auto next = lines[i++];
      if (seen.contains(next.text)) continue;
      seen.insert(next.text);
      if (translation.isEmpty()) translation = next.text;
      else extras.append(QVariantMap{{"time",next.time},{"text",next.text},{"translation",QString()}});
    }
    result.append(QVariantMap{{"time",first.time},{"text",first.text},{"translation",translation}});
    result.append(extras);
  }
  return result;
}

QVariantMap ReadNativeLyrics(const QString& path, const QString& platform, const QString& song) {
  // Local sidecar/embedded lyrics have precedence and remain with that provider.
  if (platform.isEmpty() || platform=="local" || song.isEmpty()) return {};
  sqlite3* db = nullptr;
  const auto filename = path.toUtf8();
  if (sqlite3_open_v2(filename.constData(), &db, SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX, nullptr)!=SQLITE_OK) {
    if (db) sqlite3_close(db);
    return {};
  }
  sqlite3_busy_timeout(db, 50);
  sqlite3_stmt* statement = nullptr;
  QVariantMap result;
  if (sqlite3_prepare_v2(db,"SELECT content,format FROM lyrics_cache WHERE song_id=? AND platform=? AND length(content)<=2097152 LIMIT 1",-1,&statement,nullptr)==SQLITE_OK) {
    const auto id = song.toUtf8(), source = platform.toUtf8();
    sqlite3_bind_text(statement,1,id.constData(),id.size(),SQLITE_TRANSIENT);
    sqlite3_bind_text(statement,2,source.constData(),source.size(),SQLITE_TRANSIENT);
    if (sqlite3_step(statement)==SQLITE_ROW) {
      const auto format = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement,1)));
      if (format=="lrc") {
        QString content = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement,0)),sqlite3_column_bytes(statement,0));
        const QString envelope = QStringLiteral("mconnect-lyrics-v1\n");
        if (content.startsWith(envelope)) {
          content = QJsonDocument::fromJson(content.mid(envelope.size()).toUtf8()).toVariant().toMap().value("content").toString();
        }
        const auto lines = ParseNativeLrc(content);
        if (!lines.isEmpty()) result = {{"loading",false},{"error",false},{"lines",lines},{"backend","cpp-lrc-cache"}};
      }
    }
  }
  if (statement) sqlite3_finalize(statement);
  sqlite3_close(db);
  return result;
}
