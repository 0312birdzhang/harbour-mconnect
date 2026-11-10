#include "native_lyric_loader.h"
#include "native_lyrics.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const QString envelope = "mconnect-lyrics-v1\n";
const QStringList sources = {"netease", "qq", "kugou", "bilibili", "local", "lrclib", "unknown"};

QString normalized(QString text) {
  text = text.toLower();
  for (const auto& pattern : QStringList{
      "[\\(（\\[【]\\s*(feat|ft)\\b[^\\)）\\]】]*[\\)）\\]】]",
      "[\\(（\\[【][^\\)）\\]】]*(live|现场|演唱会)[^\\)）\\]】]*[\\)）\\]】]",
      "[\\(（\\[【][^\\)）\\]】]*(remaster|重制|重置)[^\\)）\\]】]*[\\)）\\]】]",
      "\\s*-\\s*[^-]*\\b(live|remaster(ed)?|radio edit|acoustic)\\b[^-]*$"}) text.replace(QRegularExpression(pattern), " ");
  text.remove(QRegularExpression("\\s+"));
  text.remove(QRegularExpression("[’'\"“”·、,，.。\\-_/\\\\|~!！?？]+"));
  return text;
}

QVariantMap rawBundle(const QVariantMap& response) {
  const auto raw = response.value("lyrics");
  return raw.type() == QVariant::String ? QVariantMap{{"lrc", raw}} : raw.toMap();
}

bool hasRaw(const QVariantMap& bundle) {
  for (const auto& name : QStringList{"lrc", "translation", "yrc", "qrc", "romaji", "yrcTranslation"}) {
    if (!bundle.value(name).toString().trimmed().isEmpty()) return true;
  }
  return false;
}

QVariantMap cachedDocument(QString content, const QString& format, QString source) {
  if (content.startsWith(envelope)) {
    const auto data = QJsonDocument::fromJson(content.mid(envelope.size()).toUtf8()).toVariant().toMap();
    if (data.value("content").type() != QVariant::String || !sources.contains(data.value("source").toString())) return {};
    content = data.value("content").toString();
    source = data.value("source").toString();
  }
  QVariantMap bundle;
  if (format == "yrc" || format == "qrc") bundle[format] = content;
  else bundle["lrc"] = content;
  bundle["translation"] = content;
  bundle["yrcTranslation"] = content;
  auto document = BuildNativeLyricsBundle(bundle);
  document["source"] = source;
  return document;
}

QVariantMap bundleDocument(const QVariantMap& bundle, const QString& source) {
  auto document = BuildNativeLyricsBundle(bundle);
  const auto format = document.value("format").toString();
  const auto translation = bundle.value(format == "yrc" ? "yrcTranslation" : "translation").toString();
  if (!translation.trimmed().isEmpty()) document["content"] = document.value("content").toString() + '\n' + translation;
  document["source"] = source;
  return document;
}
}

QString NativeSongDedupeKey(const QVariantMap& song) {
  const auto artists = song.value("artists").toList();
  const auto primary = artists.isEmpty() ? QString() : artists.first().toMap().value("name").toString();
  const auto seconds = song.value("durationMs").toLongLong() / 1000;
  const auto bucket = seconds <= 0 ? qint64(0) : (seconds + 2) / 4;
  return normalized(song.value("name").toString()) + '|' + normalized(primary) + '|' + QString::number(bucket);
}

QString NativeClosestLrclibLyrics(const QVariant& response, qint64 durationMs) {
  auto data = response;
  if (data.type() == QVariant::String) data = QJsonDocument::fromJson(data.toString().toUtf8()).toVariant();
  if (data.type() != QVariant::List) return {};
  QString best;
  double distance = std::numeric_limits<double>::infinity();
  const auto wanted = double(durationMs) / 1000;
  for (const auto& raw : data.toList()) {
    if (raw.type() != QVariant::Map) continue;
    const auto row = raw.toMap();
    if ((row.value("instrumental").type() == QVariant::Bool && row.value("instrumental").toBool()) ||
        row.value("syncedLyrics").type() != QVariant::String || row.value("syncedLyrics").toString().trimmed().isEmpty()) continue;
    if (durationMs <= 0) return row.value("syncedLyrics").toString();
    const auto value = row.value("duration");
    const auto duration = value.type() == QVariant::String || value.type() == QVariant::Bool ? 0.0 : value.toDouble();
    const auto delta = std::abs(duration - wanted);
    if (std::isfinite(delta) && delta < distance) { distance = delta; best = row.value("syncedLyrics").toString(); }
  }
  return distance <= std::max(5.0, wanted * 0.05) ? best : QString();
}

bool NativeShouldAskLrclib(const QVariantMap& document, qint64 durationMs) {
  const auto lines = document.value("lines").toList();
  return lines.isEmpty() || (durationMs > 0 && lines.last().toMap().value("time").toLongLong() - 30000 > durationMs);
}

NativeLyricLoader::NativeLyricLoader(NativeStorage& storage, NativeMusicServices& music, QObject* parent, LrclibFetch lrclib)
    : QObject(parent), storage_(storage), music_(music), lrclib_(std::move(lrclib)) {
  connect(&music_, &NativeMusicServices::accountsChanged, this, [this]() {
    if (loading_) { ++generation_; loading_ = false; error_ = true; emit changed(); }
  });
}

QVariantMap NativeLyricLoader::snapshot() const {
  QVariantList lines;
  for (const auto& raw : document_.value("lines").toList()) {
    const auto line = raw.toMap();
    lines.append(QVariantMap{{"time", line.value("time")}, {"text", line.value("text")}, {"translation", line.value("translation", "")}});
  }
  return {{"loading", loading_}, {"error", error_}, {"lines", lines}};
}

void NativeLyricLoader::load(const QVariantMap& song, bool preferCached) {
  if (closed_) return;
  ++generation_;
  song_ = NativePlaybackSong(song);
  preferCached_ = preferCached;
  document_.clear(); error_ = false; loading_ = !song_.isEmpty();
  emit changed();
  if (loading_) schedule();
}

void NativeLyricLoader::schedule() {
  if (running_ || scheduled_ || closed_) return;
  scheduled_ = true;
  QTimer::singleShot(0, this, [this]() { scheduled_ = false; run(); });
}

void NativeLyricLoader::run() {
  if (closed_ || song_.isEmpty()) return;
  const auto generation = generation_;
  const auto song = song_;
  running_ = true;
  QVariantMap document;
  bool failed = false;
  try { document = resolve(song, generation, preferCached_); }
  catch (const NativeRequestFailure&) { failed = true; }
  catch (const NativeStorageError&) { failed = true; }
  running_ = false;
  if (closed_) return;
  if (generation != generation_) { if (loading_ && !song_.isEmpty()) schedule(); return; }
  document_ = document; error_ = failed; loading_ = false;
  emit changed();
}

QVariantMap NativeLyricLoader::resolve(const QVariantMap& song, quint64 generation, bool preferCached) {
  const auto valid = [this, generation]() { return !closed_ && generation == generation_; };
  const auto platform = song.value("platform").toString();
  try {
    const auto rows = storage_.database().query("SELECT content,format,synced_at FROM lyrics_cache WHERE song_id=? AND platform=? LIMIT 1", {song.value("id"), platform});
    if (!rows.isEmpty()) {
      const auto row = rows.first().toMap();
      const auto content = row.value("content").toString();
      const bool local = platform == "local" && !content.startsWith(envelope);
      const bool expired = QDateTime::currentMSecsSinceEpoch() - row.value("synced_at").toLongLong() >= qint64(14) * 86400000;
      if (local || preferCached || !expired) {
        auto document = cachedDocument(content, local ? QString() : row.value("format").toString(), platform);
        bool translated = false;
        for (const auto& line : document.value("lines").toList()) if (!line.toMap().value("translation").toString().trimmed().isEmpty()) translated = true;
        if (!document.value("lines").toList().isEmpty() && (local || preferCached || document.value("format") != "yrc" || translated)) return document;
      }
    }
  } catch (const NativeStorageError&) { qWarning("MCONNECT_NATIVE_LYRIC_CACHE_READ_FAILED"); }
  if (storage_.cacheSettings().value("offlineMode").toBool()) return {};
  bool answered = false, failed = false;
  QVariantMap document;
  if (platform != "local") {
    const auto own = [&](const QString& source, const QString& id) {
      try {
        const auto bundle = rawBundle(music_.request(source, "lyrics", {{"id", id}}, 8000));
        if (!valid()) return false;
        answered = true;
        if (hasRaw(bundle)) { document = bundleDocument(bundle, source); return true; }
      } catch (const NativeRequestFailure&) { failed = true; }
      return false;
    };
    bool received = own(platform, song.value("id").toString());
    if (!valid()) return {};
    if (!received) for (const auto& service : music_.platforms()) {
      const auto source = service.toMap().value("id").toString();
      if (source == platform || source == "local") continue;
      try {
        QStringList names;
        for (const auto& artist : song.value("artists").toList()) names.append(artist.toMap().value("name").toString());
        const auto keyword = (song.value("name").toString() + ' ' + names.join(' ')).trimmed();
        const auto candidates = NativeMusicSongs(music_.request(source, "search", {{"query", keyword}, {"limit", 10}, {"page", 1}}, 8000), source);
        if (!valid()) return {};
        for (const auto& raw : candidates) if (NativeSongDedupeKey(raw.toMap()) == NativeSongDedupeKey(song)) {
          received = own(source, raw.toMap().value("id").toString());
          break;
        }
      } catch (const NativeRequestFailure&) { failed = true; }
      if (!valid()) return {};
      if (received) break;
    }
  }
  if (NativeShouldAskLrclib(document, song.value("durationMs").toLongLong())) {
    QString content;
    try { content = lrclib_ ? lrclib_(song) : fetchLrclib(song); }
    catch (...) { qWarning("MCONNECT_NATIVE_LRCLIB_FAILED"); }
    if (!valid()) return {};
    const auto fallback = cachedDocument(content, "lrc", "lrclib");
    if (!fallback.value("lines").toList().isEmpty()) { cache(song, fallback); return fallback; }
  }
  if (!valid()) return {};
  if (document.value("lines").toList().isEmpty()) {
    if (!answered && failed) throw NativeRequestFailure(QStringLiteral("歌词加载失败"));
    return {};
  }
  cache(song, document);
  return document;
}

QString NativeLyricLoader::fetchLrclib(const QVariantMap& song) {
  QUrl url("https://lrclib.net/api/search");
  QUrlQuery query;
  query.addQueryItem("track_name", song.value("name").toString());
  query.addQueryItem("artist_name", NativeArtistNames(song));
  url.setQuery(query);
  QNetworkRequest request(url);
  request.setRawHeader("User-Agent", "Mconnect (lyrics fallback)");
  request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
  request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
  auto* reply = network_.get(request);
  reply->setReadBufferSize(2 * 1024 * 1024 + 1);
  QTimer timer;
  timer.setSingleShot(true);
  QEventLoop loop;
  connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  connect(reply, &QNetworkReply::readyRead, &loop, [reply, &loop]() { if (reply->bytesAvailable() > 2 * 1024 * 1024) loop.quit(); });
  timer.start(6000);
  if (!reply->isFinished()) loop.exec();
  QString lyrics;
  if (reply->isFinished() && reply->error() == QNetworkReply::NoError && reply->bytesAvailable() <= 2 * 1024 * 1024) {
    lyrics = NativeClosestLrclibLyrics(QJsonDocument::fromJson(reply->readAll()).toVariant(), song.value("durationMs").toLongLong());
  }
  reply->abort(); reply->deleteLater();
  return lyrics;
}

void NativeLyricLoader::cache(const QVariantMap& song, const QVariantMap& document) {
  auto content = document.value("content").toString();
  if (document.value("source") != song.value("platform")) {
    content = envelope + QString::fromUtf8(QJsonDocument::fromVariant(QVariantMap{{"content", content}, {"source", document.value("source")}}).toJson(QJsonDocument::Compact));
  }
  try {
    storage_.database().execute("INSERT OR REPLACE INTO lyrics_cache(song_id,platform,content,format,synced_at) VALUES(?,?,?,?,?)",
        {song.value("id"), song.value("platform"), content, document.value("format"), QDateTime::currentMSecsSinceEpoch()});
  } catch (const NativeStorageError&) { qWarning("MCONNECT_NATIVE_LYRIC_CACHE_WRITE_FAILED"); }
}

void NativeLyricLoader::close() { closed_ = true; ++generation_; song_.clear(); }
