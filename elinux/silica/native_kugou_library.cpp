#include "native_platforms.h"
#include "platform_models.h"
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>

namespace {
constexpr int requestBudget = 18000;
constexpr int maximumSongs = 10000;

QString serialize(const QVariantMap& value) {
  return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
}

QVariantMap failure(const QString& error) {
  return {{"backend", "cpp-kugou-library"}, {"error", error}};
}

QString text(const QVariantMap& data, const QStringList& keys) {
  for (const auto& key : keys) {
    const auto value = data.value(key);
    if (value.isNull() || value.type() == QVariant::Bool) continue;
    const auto normalized = value.type() == QVariant::String ? value.toString() : ParseNeteaseArtist({{"id", value}}).id;
    if (!normalized.isEmpty()) return normalized;
  }
  return {};
}

bool collectPlaylists(const QVariant& source, QVariantList& output, int depth = 0) {
  if (source.isNull()) return true;
  if (depth > 8 || output.size() > 2000) return false;
  if (source.type() == QVariant::List) {
    output.append(source.toList());
    return output.size() <= 2000;
  }
  if (source.type() != QVariant::Map) return false;
  const auto data = source.toMap();
  for (const auto& key : QStringList{"list_create_list", "list_collect_list", "list", "info", "lists", "data"}) {
    if (data.contains(key) && !collectPlaylists(data.value(key), output, depth + 1)) return false;
  }
  return true;
}

bool trustedShare(const QUrl& url) {
  return url.isValid() && url.userInfo().isEmpty() && url.toString().size() <= 16384 &&
      (url.scheme() == "https" || url.scheme() == "http") &&
      (url.port(-1) == -1 || url.port(-1) == (url.scheme() == "https" ? 443 : 80)) &&
      (url.host() == "kugou.com" || url.host().endsWith(".kugou.com"));
}

QString shareId(const QUrl& url) {
  if (!trustedShare(url)) return {};
  const QUrlQuery query(url);
  QString id;
  for (const auto& key : QStringList{"global_specialid", "global_collection_id", "specialid"}) {
    id = query.queryItemValue(key, QUrl::FullyDecoded);
    if (!id.isEmpty()) break;
  }
  if (id.isEmpty()) {
    const auto segments = url.path().split('/');
    for (const auto& name : QStringList{"songlist", "playlist"}) {
      const int index = segments.indexOf(name);
      if (index >= 0 && index + 1 < segments.size()) { id = segments[index + 1]; break; }
    }
  }
  return id != "-" && id != "-2147483648" && QRegularExpression("^[A-Za-z0-9_-]{1,256}$").match(id).hasMatch() ? id : QString();
}

bool validArguments(const QString& operation, const QVariantMap& args) {
  QSet<QString> fields;
  if (operation == "playlist") fields = {"id"};
  else if (operation == "like") fields = {"id", "like"};
  else if (operation == "collect") fields = {"id", "collect"};
  else if (operation == "create") fields = {"name"};
  else if (operation == "share") fields = {"url"};
  if (fields.size() != args.size()) return false;
  for (auto item = args.cbegin(); item != args.cend(); ++item) {
    if (!fields.contains(item.key())) return false;
    if (item.key() == "like" || item.key() == "collect") {
      if (item.value().type() != QVariant::Bool) return false;
    } else {
      if (item.value().type() != QVariant::String) return false;
      const auto value = item.value().toString();
      if (item.key() == "id" && !QRegularExpression(operation == "like" ? "^[A-Fa-f0-9]{32}$" : "^[A-Za-z0-9_-]{1,256}$").match(value).hasMatch()) return false;
      if (item.key() == "name" && (value.trimmed().isEmpty() || value.size() > 512)) return false;
      if (item.key() == "url" && value.size() > 16384) return false;
    }
  }
  return true;
}

class KugouLibraryJob : public std::enable_shared_from_this<KugouLibraryJob> {
 public:
  NativePlatformHttp* http = nullptr;
  QUrl endpoint;
  QString operation, id, route, createdName;
  QVariantMap arguments, session, createdData;
  QVariantList songs;
  QSet<QString> pages;
  int page = 1, redirects = 0;
  QElapsedTimer elapsed;
  using Reply = std::function<void(QVariantMap)>;
  Reply done;

  NativeKugouSigning signing() const {
    return session.value("client") == "lite" ? NativeKugouSigning::Lite : NativeKugouSigning::Android;
  }

  void finish(QVariantMap result) {
    result["backend"] = "cpp-kugou-library";
    if (serialize(result).toUtf8().size() > 8 * 1024 * 1024) result = failure("response_too_large");
    if (!result.contains("error")) qInfo("MCONNECT_NATIVE_KUGOU_LIBRARY_OK operation=%s", qPrintable(operation));
    done(result);
  }

  void fetch(const QUrl& logicalUrl, const QVariantMap& parameters, Reply reply, const QByteArray& body = {},
             const QByteArray& router = {}, bool safe = true, bool legacy = false) {
    const int remaining = requestBudget - int(elapsed.elapsed());
    if (remaining <= 0) { reply(failure("timeout")); return; }
    const auto base = endpoint.isEmpty() ? logicalUrl : endpoint.resolved(QUrl(logicalUrl.path()));
    QList<QPair<QString, QString>> query;
    for (auto item = parameters.cbegin(); item != parameters.cend(); ++item) query.append({item.key(), item.value().toString()});
    QNetworkRequest request(NativePlatformUrl(base, query));
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Linux; Android 11) AppleWebKit/537.36");
    if (!router.isEmpty()) request.setRawHeader("x-router", router);
    if (logicalUrl.host() == "pubsongscdn.kugou.com") request.setRawHeader("Referer", "https://activity.kugou.com/");
    if (!body.isEmpty()) request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const auto self = shared_from_this();
    http->request(request, body.isEmpty() ? "GET" : "POST", body, [self, reply](NativeHttpResult response) {
      const auto result = response.error.isEmpty() ? ParseNativeKugouResponse(response.body) : failure(response.error);
      if (!result.contains("error") && result.contains("status") && !NativeKugouSuccess(result)) {
        reply(failure("provider_error")); return;
      }
      reply(result);
    }, remaining, 8 * 1024 * 1024, safe && body.isEmpty(), true, true, legacy);
  }

  void userPlaylists(Reply reply) {
    const QVariantMap data{{"userid", session.value("userid", "0")}, {"token", session.value("token", "")},
      {"total_ver", 979}, {"type", 2}, {"page", 1}, {"pagesize", 50}};
    const auto body = QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact);
    const auto self = shared_from_this();
    fetch(QUrl("https://gateway.kugou.com/v7/get_all_list"), NativeKugouParameters(session,
      {{"plat", 1}, {"userid", session.value("userid", "0").toString().toLongLong()}, {"token", session.value("token", "")}}, body, signing()),
      [self, reply](QVariantMap result) {
        if (result.contains("error")) { reply(result); return; }
        QVariantList raw;
        if (!collectPlaylists(result.value("data"), raw) || !collectPlaylists(result, raw)) {
          reply(failure("invalid_response")); return;
        }
        QVariantList normalized;
        QSet<QString> ids;
        for (const auto& item : raw) {
          if (item.type() != QVariant::Map) { reply(failure("invalid_response")); return; }
          const auto playlist = ParseNativeKugouPlaylist(item.toMap(), true);
          const auto id = playlist.value("id").toString();
          if (id.isEmpty() || ids.contains(id)) continue;
          ids.insert(id);
          normalized.append(playlist);
        }
        if (normalized.size() > 1000) { reply(failure("response_too_large")); return; }
        reply({{"playlists", normalized}});
      }, body, "cloudlist.service.kugou.com", false);
  }

  void playlist() {
    const auto self = shared_from_this();
    const Reply reply = [self](QVariantMap result) { self->playlistPage(result); };
    if (route == "shared") {
      fetch(QUrl("https://pubsongscdn.kugou.com/v2/get_other_list_file"), NativeKugouParameters(session,
        {{"srcappid", 2919}, {"clientver", 1000}, {"appid", 1058}, {"type", 0}, {"module", "playlist"},
         {"page", page}, {"pagesize", 100}, {"global_collection_id", id}, {"uid", session.value("userid", "0")},
         {"token", session.value("token", "")}}, {}, NativeKugouSigning::H5), reply);
    } else if (route == "private") {
      const QVariantMap data{{"listid", id}, {"userid", session.value("userid", "0")}, {"token", session.value("token", "")},
        {"page", page}, {"pagesize", 100}, {"show_album_info", 1}};
      const auto body = QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact);
      fetch(QUrl("https://gateway.kugou.com/v4/get_list_all_file"), NativeKugouParameters(session, {}, body, signing()),
          reply, body, "cloudlist.service.kugou.com", false);
    } else {
      fetch(QUrl("http://mobilecdnbj.kugou.com/api/v5/special/song"), {{"specialid", id}, {"page", page},
        {"pagesize", 100}, {"plat", 2}, {"version", 11309}}, reply);
    }
  }

  void playlistPage(const QVariantMap& result) {
    if (result.contains("error")) { finish(result); return; }
    auto data = result.value("data");
    if (data.type() != QVariant::Map) data = result;
    QVariant rows;
    for (const auto& container : QList<QVariantMap>{data.toMap(), result}) {
      for (const auto& key : QStringList{"info", "songs", "list", "data", "lists"}) {
        if (container.value(key).type() == QVariant::List) { rows = container.value(key); break; }
        if (key != "data" && container.contains(key) && !container.value(key).isNull()) {
          finish(failure("invalid_response")); return;
        }
      }
      if (rows.isValid()) break;
    }
    if (!rows.isValid()) {
      if (data.type() != QVariant::Map) { finish(failure("invalid_response")); return; }
      rows = QVariantList();
    }
    const auto raw = rows.toList();
    if (raw.size() > maximumSongs) { finish(failure("response_too_large")); return; }
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument::fromVariant(raw).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    if (!raw.isEmpty() && pages.contains(hash)) { finish(failure("pagination_stalled")); return; }
    pages.insert(hash);
    const auto previousSize = songs.size();
    for (const auto& item : raw) {
      if (item.type() != QVariant::Map) { finish(failure("invalid_response")); return; }
      const auto song = ParseNativeKugouSong(item.toMap());
      if (!song.isEmpty()) songs.append(song);
    }
    if (!raw.isEmpty() && songs.size() == previousSize) { finish(failure("invalid_response")); return; }
    qint64 total = -1;
    for (const auto& container : QList<QVariantMap>{result.value("data").toMap(), result}) {
      for (const auto& key : QStringList{"count", "total", "songcount", "song_count", "song_num", "total_count"}) {
        bool valid = false;
        const auto value = container.value(key).toLongLong(&valid);
        if (valid && value > 0) { total = value; break; }
      }
      if (total > 0) break;
    }
    if (total > maximumSongs || songs.size() > maximumSongs) { finish(failure("response_too_large")); return; }
    if (raw.size() >= 100 && (total < 0 || page * 100 < total)) {
      if (page >= 100) { finish(failure("response_too_large")); return; }
      ++page;
      playlist();
      return;
    }
    if (total > songs.size()) { finish(failure("incomplete_response")); return; }
    if (songs.isEmpty() && route != "private") {
      if (route == "shared") {
        const auto parts = id.split('_');
        if (parts.size() < 4 || parts[0] != "collection" || parts[3].isEmpty() || parts[3] == "0") {
          finish({{"songs", songs}}); return;
        }
        id = parts[3];
      }
      route = "private";
      pages.clear();
      page = 1;
      playlist();
      return;
    }
    finish({{"songs", songs}});
  }

  void liked() {
    QVariantMap parameters{{"type", 1}, {"page", 1}, {"pagesize", 50}, {"appid", 1005}};
    for (const auto& key : QStringList{"token", "userid"}) {
      if (session.contains(key)) parameters[key] = session.value(key);
    }
    const auto self = shared_from_this();
    fetch(QUrl("https://wwwapi.kugou.com/uc/collection/song/list"), parameters, [self](QVariantMap result) {
      if (result.contains("error")) { self->finish(result); return; }
      const auto rows = result.value("data").toMap().value("info");
      if (rows.type() != QVariant::List || rows.toList().size() > maximumSongs) { self->finish(failure("invalid_response")); return; }
      QVariantList songs;
      for (const auto& item : rows.toList()) {
        if (item.type() != QVariant::Map) { self->finish(failure("invalid_response")); return; }
        const auto song = ParseNativeKugouSong(item.toMap());
        if (!song.isEmpty()) songs.append(song);
      }
      self->finish({{"songs", songs}});
    });
  }

  void like() {
    QVariantMap parameters{{"hash", id}, {"appid", 1014}, {"mid", "mid"}, {"plat", 0}, {"version", 11309}};
    if (session.contains("token")) parameters["token"] = session.value("token");
    const auto self = shared_from_this();
    const auto path = arguments.value("like").toBool() ? "collect" : "uncollect";
    fetch(QUrl(QString("http://mobilecdn.kugou.com/api/v5/song/%1").arg(path)), parameters,
      [self](QVariantMap result) {
        if (result.contains("error")) { self->finish(result); return; }
        self->finish({{"ok", NativeKugouSuccess(result)}});
      }, {}, {}, false, true);
  }

  void writePlaylist() {
    const bool create = operation == "create";
    if (!create && !arguments.value("collect").toBool()) { finish({{"ok", false}}); return; }
    const QVariantMap data{{"userid", session.value("userid", "0")}, {"token", session.value("token", "")},
      {"total_ver", 0}, {"name", create ? arguments.value("name").toString() : QString()},
      {"type", create ? 0 : 1}, {"source", 1}, {"is_pri", 0},
      {"list_create_userid", create ? QVariant() : session.value("userid", "0")},
      {"list_create_listid", create ? QVariant() : QVariant(id)}, {"list_create_gid", ""}, {"from_shupinmv", 0}};
    const auto body = QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact);
    QVariantMap parameters;
    if (create) parameters = {{"last_time", QDateTime::currentMSecsSinceEpoch() / 1000}, {"last_area", "gztx"},
      {"userid", session.value("userid", "0")}, {"token", session.value("token", "")}};
    const auto self = shared_from_this();
    fetch(QUrl("https://gateway.kugou.com/cloudlist.service/v5/add_list"),
      NativeKugouParameters(session, parameters, body, create ? signing() : NativeKugouSigning::Android),
      [self, create](QVariantMap result) {
        if (result.contains("error")) { self->finish(result); return; }
        if (!NativeKugouSuccess(result)) { self->finish({{"ok", false}, {"playlists", QVariantList()}}); return; }
        if (!create) { self->finish({{"ok", true}}); return; }
        self->createdData = result.value("data").toMap();
        self->createdName = self->arguments.value("name").toString();
        self->userPlaylists([self](QVariantMap refreshed) {
          if (refreshed.value("error") == "login_required") { self->finish(refreshed); return; }
          for (const auto& item : refreshed.value("playlists").toList()) {
            const auto playlist = item.toMap();
            if (playlist.value("name").toString().trimmed() == self->createdName.trimmed()) {
              self->finish({{"ok", true}, {"playlists", QVariantList{playlist}}}); return;
            }
          }
          const auto id = text(self->createdData, {"global_collection_id", "global_specialid"});
          if (id.isEmpty()) { self->finish({{"ok", true}, {"playlists", QVariantList()}}); return; }
          auto data = self->createdData;
          data["global_collection_id"] = id;
          data["name"] = self->createdName;
          const auto playlist = ParseNativeKugouPlaylist(data, true);
          self->finish({{"ok", true}, {"playlists", QVariantList{playlist}}});
        });
      }, body, {}, false);
  }

  void shareMetadata() {
    const auto self = shared_from_this();
    const Reply reply = [self](QVariantMap result) {
      if (result.value("error") == "login_required") { self->finish(result); return; }
      auto data = result.value("data").toMap();
      data["global_collection_id"] = self->id;
      if (text(data, {"specialname", "name", "playlistname", "listname"}).isEmpty()) data["name"] = QString::fromUtf8("酷狗歌单");
      auto playlist = ParseNativeKugouPlaylist(data);
      playlist["collected"] = false;
      self->finish({{"playlists", QVariantList{playlist}}});
    };
    if (id.startsWith("collection_")) {
      fetch(QUrl("https://pubsongscdn.kugou.com/v2/get_other_list_file"), NativeKugouParameters(session,
        {{"srcappid", 2919}, {"clientver", 1000}, {"appid", 1058}, {"type", 0}, {"module", "playlist"},
         {"page", 1}, {"pagesize", 1}, {"global_collection_id", id}, {"uid", session.value("userid", "0")},
         {"token", session.value("token", "")}}, {}, NativeKugouSigning::H5), reply);
    } else fetch(QUrl("http://mobilecdnbj.kugou.com/api/v5/special/detail"),
        {{"specialid", id}, {"plat", 2}, {"ver", 1000}, {"withsong", 1}}, reply);
  }

  void share(QUrl url) {
    id = shareId(url);
    if (!id.isEmpty()) { shareMetadata(); return; }
    if (!trustedShare(url) || !QStringList{"t1.kugou.com", "t.kugou.com"}.contains(url.host())) {
      finish({{"playlists", QVariantList()}}); return;
    }
    if (++redirects > 3) { finish(failure("redirect_rejected")); return; }
    if (url.scheme() == "http") url.setScheme("https");
    const int remaining = requestBudget - int(elapsed.elapsed());
    if (remaining <= 0) { finish(failure("timeout")); return; }
    const auto target = endpoint.isEmpty() ? url : endpoint.resolved(QUrl(url.path() + '?' + url.query(QUrl::FullyEncoded)));
    QNetworkRequest request(target);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Linux; Android 11) AppleWebKit/537.36");
    const auto self = shared_from_this();
    http->request(request, "GET", {}, [self, url](NativeHttpResult response) {
      if (!response.error.isEmpty()) { self->finish(failure(response.error)); return; }
      QString location;
      for (const auto& header : response.headers) {
        if (header.first.toLower() == "location") { location = QString::fromUtf8(header.second); break; }
      }
      if (location.isEmpty()) { self->finish({{"playlists", QVariantList()}}); return; }
      const auto next = url.resolved(QUrl(location));
      if (!trustedShare(next)) { self->finish(failure("redirect_rejected")); return; }
      self->share(next);
    }, remaining, 1024 * 1024, true, false, false);
  }

  void start() {
    id = arguments.value("id").toString();
    if (operation == "userPlaylists") {
      const auto self = shared_from_this();
      userPlaylists([self](QVariantMap result) { self->finish(result); });
    } else if (operation == "playlist") {
      route = id.startsWith("collection_") ? "shared" : "public";
      playlist();
    } else if (operation == "liked") liked();
    else if (operation == "like") like();
    else if (operation == "share") share(QUrl(arguments.value("url").toString(), QUrl::StrictMode));
    else writePlaylist();
  }
};
}

QString NativePlatforms::KugouLibrary(const QString& operation, const QString& arguments, const QString& encodedSession) {
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(arguments.toUtf8(), &error);
  const auto session = ParseNativeKugouSession(encodedSession);
  if (!QStringList{"userPlaylists", "playlist", "liked", "like", "create", "collect", "share"}.contains(operation) ||
      arguments.toUtf8().size() > 16384 || error.error != QJsonParseError::NoError || !document.isObject() ||
      session.contains("error") || !validArguments(operation, document.toVariant().toMap())) {
    return serialize(failure("invalid_arguments"));
  }
  if (!calledFromDBus()) return serialize(failure("dbus_required"));
  if (QStringList{"userPlaylists", "liked", "like", "create", "collect"}.contains(operation) &&
      (session.value("token").toString().isEmpty() || session.value("userid").toString().isEmpty())) {
    return serialize(failure("login_required"));
  }
  setDelayedReply(true);
  const auto bus = connection();
  const auto call = message();
  auto job = std::make_shared<KugouLibraryJob>();
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
