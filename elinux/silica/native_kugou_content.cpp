#include "native_platforms.h"
#include "platform_models.h"
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <zlib.h>

namespace {
const QStringList operations{"searchPlaylists", "stream", "qualities", "lyrics", "lyricsByInfo"};
const QStringList qualityNames{"128", "320", "320", "flac", "high", "viper_clear", "viper_atmos", "viper_tape"};
constexpr int requestBudget = 18000;
constexpr int maximumLyrics = 4 * 1024 * 1024;

QString serialize(const QVariantMap& value) {
  return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
}

QVariantMap failure(const QString& error) {
  return {{"backend", "cpp-kugou-content"}, {"error", error}};
}

qint64 integer(const QVariant& value, qint64 fallback = -1) {
  bool valid = false;
  const auto result = value.toLongLong(&valid);
  return valid && value.type() != QVariant::Bool &&
      (value.type() != QVariant::Double || value.toDouble() == double(result)) ? result : fallback;
}

QString text(const QVariantMap& value, const QStringList& keys) {
  for (const auto& key : keys) {
    const auto field = value.value(key);
    if (field.isNull() || field.type() == QVariant::Bool) continue;
    const auto result = field.type() == QVariant::String ? field.toString().trimmed() :
        ParseNeteaseArtist({{"id", field}}).id;
    if (!result.isEmpty() && result != "null") return result;
  }
  return {};
}

bool matches(const QString& value, const char* pattern) {
  return QRegularExpression(pattern).match(value).hasMatch();
}

QString webUrl(QString value) {
  value = value.trimmed();
  if (value.startsWith("//")) value = "https:" + value;
  const QUrl url(value);
  return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() &&
      (url.scheme() == "http" || url.scheme() == "https") && value.size() <= 16384 ? value : QString();
}

QString playableUrl(const QVariant& value, int depth = 0) {
  if (depth > 4) return {};
  if (value.type() == QVariant::String) return webUrl(value.toString());
  if (value.type() == QVariant::List) {
    for (const auto& item : value.toList()) {
      const auto url = playableUrl(item, depth + 1);
      if (!url.isEmpty()) return url;
    }
    return {};
  }
  if (value.type() != QVariant::Map) return {};
  const auto data = value.toMap();
  for (const auto& key : QStringList{"url", "play_url", "playUrl", "playurl", "audio_url", "audioUrl",
      "download_url", "downloadUrl", "backup_url", "backupUrl", "backup_urls", "backupUrls",
      "play_backup_url", "playBackupUrl", "play_backup_urls", "playBackupUrls",
      "data", "info", "song_info", "songInfo", "audio_info", "audioInfo", "file"}) {
    const auto url = playableUrl(data.value(key), depth + 1);
    if (!url.isEmpty()) return url;
  }
  return {};
}

QVariantMap responseObject(QByteArray body) {
  auto decoded = QString::fromUtf8(body.constData(), body.size());
  if (decoded.toUtf8() != body) return failure("invalid_response");
  decoded.replace("<!--KG_TAG_RES_START-->", "");
  decoded.replace("<!--KG_TAG_RES_END-->", "");
  body = decoded.trimmed().toUtf8();
  for (int depth = 0; depth < 3; ++depth) {
    QJsonParseError error;
    auto document = QJsonDocument::fromJson(body, &error);
    if (error.error == QJsonParseError::NoError && document.isObject()) {
      auto result = document.toVariant().toMap();
      if (result.contains("error") && (result.value("error").isNull() || result.value("error").toString().isEmpty())) result.remove("error");
      return result;
    }
    document = QJsonDocument::fromJson('[' + body + ']', &error);
    if (error.error != QJsonParseError::NoError || !document.isArray() || document.array().size() != 1 ||
        !document.array().first().isString()) return failure("invalid_response");
    body = document.array().first().toString().toUtf8();
  }
  return failure("invalid_response");
}

QVariantMap parseSession(const QString& encoded) {
  if (encoded.toUtf8().size() > 32768) return failure("invalid_arguments");
  QVariantMap data;
  if (encoded.trimmed().startsWith('{')) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(encoded.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return failure("invalid_arguments");
    data = document.toVariant().toMap();
  } else if (!encoded.isEmpty()) {
    const auto parts = encoded.split('|');
    data["token"] = parts.first();
    if (parts.size() > 1) data["userid"] = parts[1];
  }
  const QSet<QString> fields{"token", "userid", "vip_token", "vip_type", "dfid", "mid", "uuid", "client"};
  QVariantMap session;
  for (auto item = data.cbegin(); item != data.cend(); ++item) {
    if (!fields.contains(item.key())) return failure("invalid_arguments");
    if (item.value().isNull()) continue;
    if (item.value().type() == QVariant::Bool ||
        (item.value().type() != QVariant::String && integer(item.value()) < 0)) return failure("invalid_arguments");
    const auto value = text(data, {item.key()});
    if (value.isEmpty() && item.value().type() != QVariant::String) return failure("invalid_arguments");
    if (value.isEmpty() || value == "null") continue;
    if (value.size() > 8192) return failure("invalid_arguments");
    if (item.key() != "client") {
      for (const auto character : value) {
        if (character.unicode() < 32 || character.unicode() > 126) return failure("invalid_arguments");
      }
    }
    session[item.key()] = value;
  }
  const auto client = session.value("client").toString().toLower();
  const bool lite = QStringList{"lite", "concept", "kugou_concept", "kugou_lite",
      QString::fromUtf8("概念版"), QString::fromUtf8("酷狗概念版")}.contains(client);
  session["client"] = lite ? "lite" : "android";
  if (session.contains("userid") && !matches(session.value("userid").toString(), "^[0-9]{1,18}$")) {
    return failure("invalid_arguments");
  }
  return session;
}

bool validArguments(const QString& operation, const QVariantMap& arguments) {
  QSet<QString> fields{"id"};
  if (operation == "stream") fields.insert("quality");
  if (operation == "searchPlaylists") fields = {"query", "page", "limit"};
  if (operation == "lyricsByInfo") fields = {"query", "duration"};
  if (fields.size() != arguments.size()) return false;
  for (auto item = arguments.cbegin(); item != arguments.cend(); ++item) {
    if (!fields.contains(item.key())) return false;
    if (item.key() == "id") {
      if (item.value().type() != QVariant::String ||
          (operation != "qualities" && !matches(item.value().toString(), "^[A-Fa-f0-9]{32}$"))) return false;
    } else if (item.key() == "query") {
      if (item.value().type() != QVariant::String || item.value().toString().trimmed().isEmpty() ||
          item.value().toString().size() > 512) return false;
    } else {
      const auto value = integer(item.value());
      if (item.value().type() == QVariant::String || value < 0) return false;
      if (item.key() == "page" && (value < 1 || value > 100)) return false;
      if (item.key() == "limit" && (value < 1 || value > 50)) return false;
      if (item.key() == "quality" && value > 7) return false;
      if (item.key() == "duration" && value > 604800000) return false;
    }
  }
  return true;
}

QString digest(const QByteArray& value) {
  return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Md5).toHex());
}

QString decodeLyrics(const QVariantMap& data, const QString& format) {
  const auto value = data.value("content");
  if (value.type() != QVariant::String || value.toString().size() > 8 * 1024 * 1024) return {};
  auto content = value.toString();
  auto base64 = content;
  base64.replace('-', '+');
  base64.replace('_', '/');
  const bool encoded = matches(base64, "^[A-Za-z0-9+/]*={0,2}$") && base64.size() % 4 != 1 &&
      (!base64.endsWith('=') || base64.size() % 4 == 0);
  const auto bytes = encoded ? QByteArray::fromBase64(base64.toLatin1()) : QByteArray();
  if (format == "lrc" || integer(data.value("contenttype")) != 0) {
    const auto decoded = QString::fromUtf8(bytes.constData(), bytes.size());
    if (encoded && decoded.toUtf8() == bytes && bytes.size() <= maximumLyrics) return decoded;
    return content.toUtf8().size() <= maximumLyrics ? content : QString();
  }
  if (!encoded || bytes.size() <= 4) return {};
  const unsigned char key[]{0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47,
                            0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69};
  auto compressed = bytes.mid(4);
  for (int index = 0; index < compressed.size(); ++index) {
    compressed[index] = char(static_cast<unsigned char>(compressed[index]) ^ key[index % 16]);
  }
  QByteArray inflated(maximumLyrics + 1, '\0');
  uLongf length = inflated.size();
  if (uncompress(reinterpret_cast<Bytef*>(inflated.data()), &length,
      reinterpret_cast<const Bytef*>(compressed.constData()), compressed.size()) != Z_OK ||
      length > maximumLyrics) return {};
  inflated.resize(int(length));
  const auto result = QString::fromUtf8(inflated.constData(), inflated.size());
  return result.toUtf8() == inflated ? result : QString();
}

class KugouContentJob : public std::enable_shared_from_this<KugouContentJob> {
 public:
  NativePlatformHttp* http = nullptr;
  QUrl endpoint;
  QString operation, id, playbackHash, albumId, albumAudioId, ordinaryUrl;
  QVariantMap arguments, session, songInfo;
  int quality = 0;
  QList<QPair<QString, qint64>> lyricQueries;
  QElapsedTimer elapsed;
  using Reply = std::function<void(QVariantMap)>;
  Reply done;

  void finish(QVariantMap result) {
    result["backend"] = "cpp-kugou-content";
    if (serialize(result).toUtf8().size() > 8 * 1024 * 1024) result = failure("response_too_large");
    if (!result.contains("error")) qInfo("MCONNECT_NATIVE_KUGOU_CONTENT_OK operation=%s", qPrintable(operation));
    done(result);
  }

  QVariantMap signedParameters(QVariantMap parameters, const QByteArray& body, bool lite = false) {
    return NativeKugouParameters(session, parameters, body,
        lite ? NativeKugouSigning::Lite : NativeKugouSigning::Android);
  }

  void fetch(const QUrl& logicalUrl, QVariantMap parameters, Reply reply, const QByteArray& body = {}) {
    const int remaining = requestBudget - int(elapsed.elapsed());
    if (remaining <= 0) { reply(failure("timeout")); return; }
    const auto base = endpoint.isEmpty() ? logicalUrl : endpoint.resolved(QUrl(logicalUrl.path()));
    QList<QPair<QString, QString>> query;
    for (auto item = parameters.cbegin(); item != parameters.cend(); ++item) query.append({item.key(), item.value().toString()});
    QNetworkRequest request(NativePlatformUrl(base, query));
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Linux; Android 11) AppleWebKit/537.36");
    if (logicalUrl.host() == "complexsearch.kugou.com") request.setRawHeader("x-router", "complexsearch.kugou.com");
    if (logicalUrl.path() == "/v5/url") request.setRawHeader("x-router", "trackercdn.kugou.com");
    if (!body.isEmpty()) request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const auto self = shared_from_this();
    http->request(request, body.isEmpty() ? "GET" : "POST", body, [self, reply](NativeHttpResult response) {
      reply(response.error.isEmpty() ? responseObject(response.body) : failure(response.error));
    }, remaining, 8 * 1024 * 1024, body.isEmpty());
  }

  QString infoText(const QStringList& fields) const {
    for (const auto& row : QList<QVariantMap>{songInfo, songInfo.value("data").toMap(), songInfo.value("info").toMap(),
        songInfo.value("audio_info").toMap(), songInfo.value("audioInfo").toMap()}) {
      const auto value = text(row, fields);
      if (!value.isEmpty()) return value;
    }
    return {};
  }

  void stream() {
    const auto self = shared_from_this();
    fetch(QUrl("https://m.kugou.com/app/i/getSongInfo.php"), {{"cmd", "playInfo"}, {"hash", id}}, [self](QVariantMap result) {
      if (result.contains("error")) { self->finish(result); return; }
      self->songInfo = result;
      self->ordinaryUrl = playableUrl(result);
      if (self->quality == 0 && !self->ordinaryUrl.isEmpty()) { self->finish({{"url", self->ordinaryUrl}}); return; }
      const QStringList keys = self->quality == 0 ? QStringList{"128hash", "hash"} : self->quality <= 2 ?
          QStringList{"320hash", "highhash", "128hash", "hash"} : self->quality == 3 ?
          QStringList{"sqhash", "320hash", "128hash", "hash"} : QStringList{"highhash", "sqhash", "320hash", "128hash", "hash"};
      self->playbackHash = self->id;
      for (const auto& key : keys) {
        QString candidate;
        for (const auto& row : QList<QVariantMap>{result.value("extra").toMap(), result, result.value("trans_param").toMap()}) {
          candidate = text(row, {key});
          if (!candidate.isEmpty()) break;
        }
        if (matches(candidate, "^[A-Fa-f0-9]{32}$")) { self->playbackHash = candidate.toLower(); break; }
      }
      self->albumId = self->infoText({"albumid", "album_id", "req_albumid"});
      self->albumAudioId = self->infoText({"album_audio_id", "audio_id", "mixsongid", "MixSongID"});
      if (!self->session.value("token").toString().isEmpty() && !self->session.value("userid").toString().isEmpty() &&
          !self->session.value("vip_token").toString().isEmpty()) self->privateStream();
      else self->ordinaryStream(false);
    });
  }

  void privateStream() {
    const auto user = session.value("userid", "0").toString();
    const auto mid = session.value("mid", "mid").toString();
    const auto preferred = qualityNames[quality];
    QVariantList preferences{preferred};
    for (const auto& name : QStringList{"flac", "high", "viper_clear", "viper_atmos", "viper_tape", "320", "128", "multitrack", "super"}) {
      if (name != preferred) preferences.append(name);
    }
    const QVariantMap data{{"area_code", "1"}, {"behavior", "play"}, {"qualities", preferences},
      {"resource", QVariantMap{{"album_audio_id", qMax<qint64>(0, integer(albumAudioId))}, {"collect_list_id", "3"},
        {"collect_time", QDateTime::currentMSecsSinceEpoch()}, {"hash", playbackHash}, {"id", 0}, {"page_id", 1}, {"type", "audio"}}},
      {"token", session.value("token", "")}, {"userid", user}, {"vip", session.value("vip_type", 0)},
      {"tracker_param", QVariantMap{{"all_m", 1}, {"auth", ""}, {"is_free_part", 0},
        {"key", digest((playbackHash + "185672dd44712f60bb1736df5a377e823116" + mid + user).toUtf8())},
        {"module_id", 0}, {"need_climax", 1}, {"need_xcdn", 1}, {"open_time", ""}, {"pid", "411"},
        {"pidversion", "3001"}, {"priv_vip_type", "6"}, {"viptoken", session.value("vip_token", "")}}}};
    const auto body = QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact);
    const auto self = shared_from_this();
    fetch(QUrl("https://tracker.kugou.com/v6/priv_url"), signedParameters({}, body, true), [self](QVariantMap result) {
      if (result.value("error") == "login_required") { self->finish(result); return; }
      const auto url = playableUrl(result);
      if (!result.contains("error") && !url.isEmpty()) { self->finish({{"url", url}}); return; }
      self->ordinaryStream(false);
    }, body);
  }

  void ordinaryStream(bool lite) {
    const auto user = session.value("userid", "0").toString();
    const auto mid = session.value("mid", "mid").toString();
    const auto secret = lite ? "185672dd44712f60bb1736df5a377e82" : "57ae12eb6890223e355ccfcb74edf70d";
    const auto appid = lite ? "3116" : "1005";
    const QVariantMap fields{{"album_id", qMax<qint64>(0, integer(albumId))}, {"area_code", 1}, {"hash", playbackHash},
      {"ssa_flag", "is_fromtrack"}, {"version", 11430}, {"page_id", lite ? 967177915 : 151369488},
      {"quality", qualityNames[quality]}, {"album_audio_id", qMax<qint64>(0, integer(albumAudioId))},
      {"behavior", "play"}, {"pid", lite ? 411 : 2}, {"cmd", 26}, {"pidversion", 3001}, {"IsFreePart", 0},
      {"ppage_id", lite ? "356753938,823673182,967485191" : "463467626,350369493,788954147"},
      {"cdnBackup", 1}, {"module", ""}, {"clientver", 11430},
      {"key", digest((playbackHash + secret + appid + mid + user).toUtf8())}};
    const auto self = shared_from_this();
    fetch(QUrl("https://gateway.kugou.com/v5/url"), signedParameters(fields, {}, lite), [self, lite](QVariantMap result) {
      if (result.value("error") == "login_required") { self->finish(result); return; }
      const auto url = playableUrl(result);
      if (!result.contains("error") && !url.isEmpty()) { self->finish({{"url", url}}); return; }
      if (!lite) { self->ordinaryStream(true); return; }
      if (!self->ordinaryUrl.isEmpty()) {
        qInfo("MCONNECT_NATIVE_KUGOU_QUALITY_DOWNGRADED");
        self->finish({{"url", self->ordinaryUrl}});
      } else self->finish(failure("unavailable"));
    });
  }

  void searchPlaylists() {
    const auto fields = signedParameters({{"keyword", arguments.value("query")}, {"page", arguments.value("page")},
      {"pagesize", arguments.value("limit")}, {"platform", "AndroidFilter"}, {"nocollect", 0}, {"iscorrection", 1}}, {});
    const auto self = shared_from_this();
    fetch(QUrl("https://complexsearch.kugou.com/v1/search/special"), fields, [self](QVariantMap result) {
      if (result.contains("error")) { self->finish(result); return; }
      if (result.contains("status") && integer(result.value("status")) != 1) { self->finish(failure("provider_error")); return; }
      const auto data = result.value("data").toMap();
      auto raw = data.value("lists", data.value("info", data.value("list")));
      if (raw.type() != QVariant::List || raw.toList().size() > 1000) { self->finish(failure("invalid_response")); return; }
      QVariantList playlists;
      for (const auto& value : raw.toList()) {
        if (value.type() != QVariant::Map) { self->finish(failure("invalid_response")); return; }
        const auto normalized = ParseNativeKugouPlaylist(value.toMap());
        if (!normalized.isEmpty()) playlists.append(normalized);
      }
      self->finish({{"playlists", playlists}});
    });
  }

  void lyrics() {
    const auto self = shared_from_this();
    fetch(QUrl("https://krcs.kugou.com/search"), {{"ver", 1}, {"man", "yes"}, {"client", "mobi"},
      {"keyword", ""}, {"duration", ""}, {"hash", id}, {"album_audio_id", ""}}, [self](QVariantMap result) {
      const auto candidates = result.value("candidates").toList();
      if (!result.contains("error") && !candidates.isEmpty()) { self->downloadLyrics(candidates.first().toMap()); return; }
      self->lyricsMetadata();
    });
  }

  void lyricsMetadata() {
    const auto self = shared_from_this();
    fetch(QUrl("https://m.kugou.com/app/i/getSongInfo.php"), {{"cmd", "playInfo"}, {"hash", id}}, [self](QVariantMap result) {
      if (result.contains("error")) { self->finish(result); return; }
      const auto singer = text(result, {"singerName", "author_name", "choricSinger"});
      const auto name = text(result, {"songName"});
      auto duration = qMax<qint64>(0, integer(result.value("timeLength")));
      if (duration == 0) duration = qMax<qint64>(0, integer(result.value("climax_info").toMap().value("timelength")));
      duration = qMin<qint64>(duration, 604800000);
      if (!singer.isEmpty() && !name.isEmpty()) self->lyricQueries.append({singer + '-' + name, duration});
      if (!name.isEmpty()) self->lyricQueries.append({name, duration});
      if (!name.isEmpty() && duration > 1000) self->lyricQueries.append({name, duration / 1000});
      self->keywordLyrics();
    });
  }

  void keywordLyrics() {
    if (lyricQueries.isEmpty()) { finish({{"lyrics", QVariantMap()}}); return; }
    const auto query = lyricQueries.takeFirst();
    const auto self = shared_from_this();
    fetch(QUrl("https://lyrics.kugou.com/search"), {{"ver", 1}, {"man", "yes"}, {"client", "pc"},
      {"keyword", query.first}, {"duration", query.second}, {"hash", ""}}, [self](QVariantMap result) {
      if (result.contains("error")) { self->finish(result); return; }
      const auto candidates = result.value("candidates").toList();
      if (candidates.isEmpty()) self->keywordLyrics();
      else self->downloadLyrics(candidates.first().toMap());
    });
  }

  void downloadLyrics(const QVariantMap& candidate, bool lrc = false) {
    const auto lyricId = text(candidate, {"id"}), accessKey = text(candidate, {"accesskey"});
    if (!matches(lyricId, "^[A-Za-z0-9_-]{1,64}$") || accessKey.isEmpty() || accessKey.size() > 1024) {
      finish({{"lyrics", QVariantMap()}}); return;
    }
    const auto format = lrc ? QString("lrc") : QString("krc");
    const auto self = shared_from_this();
    fetch(QUrl("https://lyrics.kugou.com/download"), {{"ver", 1}, {"client", "pc"}, {"id", lyricId},
      {"accesskey", accessKey}, {"fmt", format}, {"charset", "utf8"}}, [self, candidate, format, lrc](QVariantMap result) {
      const auto content = result.contains("error") ? QString() : decodeLyrics(result, format);
      if (!content.isEmpty()) { self->finish({{"lyrics", QVariantMap{{"lrc", content}}}}); return; }
      if (!lrc) self->downloadLyrics(candidate, true);
      else self->finish({{"lyrics", QVariantMap()}});
    });
  }

  void start() {
    id = arguments.value("id").toString().toLower();
    quality = arguments.value("quality").toInt();
    if (operation == "stream") { stream(); return; }
    if (operation == "searchPlaylists") { searchPlaylists(); return; }
    if (operation == "lyrics") { lyrics(); return; }
    if (operation == "lyricsByInfo") {
      lyricQueries.append({arguments.value("query").toString(), arguments.value("duration").toLongLong()});
      keywordLyrics();
      return;
    }
    QVariantList qualities;
    for (const auto& item : QList<QVariantMap>{{{"level", 0}, {"bitrate", 128000}, {"format", "mp3"}},
        {{"level", 1}, {"bitrate", 320000}, {"format", "mp3"}}, {{"level", 3}, {"bitrate", 999000}, {"format", "flac"}},
        {{"level", 4}, {"bitrate", 2400000}, {"format", "flac"}}, {{"level", 5}, {"bitrate", 999000}, {"format", "flac"}}}) qualities.append(item);
    finish({{"qualities", qualities}});
  }
};
}

QVariantMap ParseNativeKugouResponse(QByteArray body) { return responseObject(body); }
QVariantMap ParseNativeKugouSession(const QString& encoded) { return parseSession(encoded); }

QVariantMap ParseNativeKugouPlaylist(const QVariantMap& row, bool editable) {
  NativePlaylist playlist;
  playlist.platform = "kugou";
  playlist.id = text(row, {"global_collection_id", "global_specialid", "specialid", "listid", "id"});
  playlist.name = text(row, {"specialname", "name", "playlistname", "listname"});
  if (playlist.id.isEmpty() || playlist.name.isEmpty()) return {};
  playlist.songCount = qMax<qint64>(0, integer(text(row, {"songcount", "song_count", "total", "count", "song_num"})));
  auto cover = text(row, {"imgurl", "image", "pic", "cover"});
  cover.replace("{size}", "480");
  playlist.coverUrl = webUrl(cover);
  playlist.creatorName = text(row, {"nickname", "username"});
  auto result = playlist.json();
  result["editable"] = editable;
  result["collected"] = !editable;
  const auto editId = text(row, {"listid", "list_create_listid"});
  if (!editId.isEmpty() && editId != playlist.id) result["editId"] = editId;
  return result;
}

QVariantMap NativeKugouParameters(const QVariantMap& session, const QVariantMap& parameters,
    const QByteArray& body, NativeKugouSigning signing) {
  const bool h5 = signing == NativeKugouSigning::H5;
  const bool web = signing == NativeKugouSigning::Web || h5;
  const bool lite = signing == NativeKugouSigning::Lite ||
      (signing == NativeKugouSigning::Web && session.value("client") == "lite");
  QVariantMap result{{"dfid", h5 ? QVariant("-") : session.value("dfid", "-")},
      {"mid", h5 ? QVariant("mid") : session.value("mid", "mid")},
      {"uuid", h5 ? QVariant("uuid") : session.value("uuid", "-")},
      {"clienttime", QDateTime::currentMSecsSinceEpoch() / (h5 ? 1 : 1000)}};
  if (!h5) {
    result["appid"] = lite ? 3116 : 1005;
    result["clientver"] = lite ? 11440 : 20489;
    for (const auto& key : QStringList{"token", "userid"}) {
      if (session.contains(key)) result[key] = session.value(key);
    }
  }
  for (auto item = parameters.cbegin(); item != parameters.cend(); ++item) result[item.key()] = item.value();
  const QByteArray secret = web ? "NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt" :
      lite ? "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA" : "OIlwieks28dk2k092lksi2UIkp";
  QByteArray material = secret;
  for (auto item = result.cbegin(); item != result.cend(); ++item) {
    const auto value = item.value().type() == QVariant::Map || item.value().type() == QVariant::List ?
        QJsonDocument::fromVariant(item.value()).toJson(QJsonDocument::Compact) : item.value().toString().toUtf8();
    material += item.key().toUtf8() + '=' + value;
  }
  result["signature"] = digest(material + (web ? QByteArray() : body) + secret);
  return result;
}

QString NativePlatforms::KugouContent(const QString& operation, const QString& arguments, const QString& encodedSession) {
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(arguments.toUtf8(), &error);
  const auto session = parseSession(encodedSession);
  if (!operations.contains(operation) || arguments.toUtf8().size() > 16384 ||
      error.error != QJsonParseError::NoError || !document.isObject() || session.contains("error") ||
      !validArguments(operation, document.toVariant().toMap())) return serialize(failure("invalid_arguments"));
  if (!calledFromDBus()) return serialize(failure("dbus_required"));
  setDelayedReply(true);
  const auto bus = connection();
  const auto call = message();
  auto job = std::make_shared<KugouContentJob>();
  job->http = &http_;
  job->endpoint = kugouEndpoint_;
  job->operation = operation;
  job->arguments = document.toVariant().toMap();
  job->session = session;
  job->done = [bus, call](const QVariantMap& result) { bus.send(call.createReply(QVariantList{serialize(result)})); };
  job->elapsed.start();
  job->start();
  return {};
}
