#include "native_music.h"
#include "native_platforms.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <cmath>

namespace {
const QStringList serviceIds = {"netease", "qq", "kugou", "bilibili"};
const QStringList serviceNames = {QStringLiteral("网易云音乐"), QStringLiteral("QQ音乐"), QStringLiteral("酷狗音乐"), QStringLiteral("哔哩哔哩音乐")};
const QStringList qualityNames = {"low", "medium", "high", "lossless", "hires", "spatial", "dolby", "master"};

QString json(const QVariant& value) {
  return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
}

QVariantMap busRequest(const QString& method, const QVariantList& arguments, int timeoutMs) {
  auto message = QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Silica",
      "/org/sailfishos/Mconnect/Platforms", "org.sailfishos.Mconnect.Platforms", method);
  message.setArguments(arguments);
  // A dedicated client connection also exercises Qt's delayed D-Bus reply
  // path when the native providers and core share one process.
  const auto client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "mconnect-native-provider-client");
  QDBusPendingCallWatcher watcher(client.asyncCall(message, timeoutMs));
  QEventLoop loop;
  QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
  if (!watcher.isFinished()) loop.exec();
  QDBusPendingReply<QString> reply = watcher;
  if (reply.isError()) throw NativeRequestFailure(QStringLiteral("平台接口暂不可用，请稍后重试"), false, "network");
  const auto raw = reply.value().toUtf8();
  if (raw.size() > 8 * 1024 * 1024) throw NativeRequestFailure(QStringLiteral("平台返回的数据过大"));
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(raw, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    throw NativeRequestFailure(QStringLiteral("平台返回的数据无效"));
  }
  return document.toVariant().toMap();
}

QString qqIdentity(const QString& cookie) {
  for (const auto& key : QStringList{"uin", "qqmusic_uin", "loginUin", "musicid", "web_uin", "wxuin"}) {
    const auto match = QRegularExpression("(?:^|;)\\s*" + key + "=o?([0-9]+)(?:;|$)").match(cookie);
    if (match.hasMatch() && match.captured(1) != "0") return match.captured(1);
  }
  return {};
}

bool shareHost(const QString& platform, const QUrl& url) {
  const QStringList domains = platform == "netease" ? QStringList{"music.163.com", "163cn.tv"} : QStringList{"y.qq.com"};
  for (const auto& domain : domains) if (url.host() == domain || url.host().endsWith('.' + domain)) return true;
  return false;
}

QString qqPlaylistId(const QUrl& url) {
  const auto query = QUrlQuery(url);
  for (const auto& name : QStringList{"id", "disstid", "dissid"}) {
    const auto id = query.queryItemValue(name);
    if (!id.isEmpty()) return id;
  }
  return QRegularExpression("(?:playlist/|taoge/)([0-9]+)(?:\\.html)?(?:/|$)").match(url.path()).captured(1);
}

QUrl resolveShare(const QString& platform, QUrl url) {
  NativePlatformHttp http;
  QElapsedTimer elapsed;
  elapsed.start();
  for (int redirects = 0; redirects < 4; ++redirects) {
    if (!shareHost(platform, url) || !url.userInfo().isEmpty() || !QStringList{"http", "https"}.contains(url.scheme())) {
      throw NativeRequestFailure(QStringLiteral("歌单链接无效"));
    }
    if (url.scheme() == "http") url.setScheme("https");
    NativeHttpResult response;
    QEventLoop loop;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0");
    http.request(request, "GET", {}, [&](NativeHttpResult value) { response = std::move(value); loop.quit(); },
                 qMax(1, 8000 - int(elapsed.elapsed())), 1024 * 1024, true, false, false);
    loop.exec();
    if (!response.error.isEmpty()) throw NativeRequestFailure(QStringLiteral("歌单链接暂时无法读取"));
    QString location;
    for (const auto& header : response.headers) if (header.first.toLower() == "location") location = QString::fromUtf8(header.second);
    if (response.status < 300 || response.status >= 400 || location.isEmpty()) return url;
    url = url.resolved(QUrl(location));
  }
  throw NativeRequestFailure(QStringLiteral("歌单链接跳转过多"));
}
}

QString NativeIdentifier(const QVariant& value, int maximum) {
  if (value.type() != QVariant::String || value.toString().trimmed().isEmpty() || value.toString().size() > maximum) {
    throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  }
  return value.toString();
}

QString NativeServicePlatform(const QVariant& value) {
  const auto id = NativeIdentifier(value);
  if (!serviceIds.contains(id)) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  return id;
}

int NativeIntegerArgument(const QVariant& value, int minimum, int maximum) {
  bool valid = false;
  const auto number = value.toLongLong(&valid);
  if (!valid || value.type() == QVariant::Bool || value.type() == QVariant::String ||
      (value.type() == QVariant::Double && (!std::isfinite(value.toDouble()) || value.toDouble() != double(number))) ||
      number < minimum || number > maximum) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  return int(number);
}

int NativeQualityIndex(const QVariant& value) {
  if (value.type() != QVariant::String || !qualityNames.contains(value.toString())) {
    throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  }
  return qualityNames.indexOf(value.toString());
}

QString NativeQualityName(int index) {
  return qualityNames[qBound(0, index, 7)];
}

bool NativeQualityLossless(int index) { return index == 3 || index == 4 || index == 7; }

QString NativeQualityLabel(int index, const QString& platform) {
  if (platform == "local") return QStringLiteral("本地");
  const QStringList common = {QStringLiteral("标准"), QStringLiteral("较高"), QStringLiteral("极高"), QStringLiteral("无损"),
      "Hi-Res", QStringLiteral("高清环绕声"), QStringLiteral("沉浸环绕声"), QStringLiteral("超清母带")};
  const QStringList qq = {QStringLiteral("标准音质"), QStringLiteral("HQ高品质"), QStringLiteral("HQ高品质"), QStringLiteral("SQ无损品质"),
      "Hi-Res", QStringLiteral("臻品全景声2.0"), QStringLiteral("臻品音质2.0"), QStringLiteral("臻品母带2.0")};
  const QStringList kugou = {QStringLiteral("标准音质"), QStringLiteral("高品音质"), QStringLiteral("超品音质"), QStringLiteral("无损音质"),
      "Hi-Res", "VIPER HiFi", QStringLiteral("臻品音质"), "DSD"};
  return (platform == "qq" ? qq : platform == "kugou" ? kugou : common)[qBound(0, index, 7)];
}

QVariantList NativeMusicSongs(const QVariantMap& response, const QString& platform) {
  if (response.value("songs").type() != QVariant::List || response.value("songs").toList().size() > 10000) {
    throw NativeRequestFailure(QStringLiteral("歌曲列表暂不可用"));
  }
  QVariantList result;
  for (const auto& raw : response.value("songs").toList()) {
    const auto song = NativePlaybackSong(raw.toMap());
    if (!NativeValidSong(song) || song.value("platform") != platform) {
      throw NativeRequestFailure(QStringLiteral("歌曲列表数据无效"));
    }
    result.append(song);
  }
  return result;
}

NativeMusicServices::NativeMusicServices(NativeStorage& storage, QObject* parent, ProviderCall call)
    : QObject(parent), storage_(storage), call_(std::move(call)) {
  for (const auto& platform : serviceIds) {
    Account account;
    try {
      storage_.recoverSession(platform);
      const auto document = QJsonDocument::fromJson(storage_.credential("user_" + platform).toString().toUtf8());
      const auto user = document.toVariant().toMap();
      account.session = NativeSessionCookie(platform, storage_.credential("cookie_" + platform).toString(), user);
      account.user = NativeSessionUser(platform, account.session, user);
      if (!user.isEmpty() && account.user.isEmpty()) account.session.clear();
    } catch (const NativeStorageError&) {
      account = {};
      qWarning("MCONNECT_NATIVE_SESSION_RESTORE_FAILED platform=%s", qPrintable(platform));
    }
    accounts_.insert(platform, account);
  }
}

QVariantList NativeMusicServices::platforms() const {
  QVariantList result;
  for (int index = 0; index < serviceIds.size(); ++index) result.append(QVariantMap{{"id", serviceIds[index]}, {"name", serviceNames[index]}});
  return result;
}

QVariantList NativeMusicServices::discoveryPlatforms() const {
  QVariantList result;
  for (const auto& raw : platforms()) {
    auto row = raw.toMap();
    const auto platform = row.value("id").toString();
    row["daily"] = platform != "bilibili";
    row["newSongs"] = platform == "netease" || platform == "qq";
    row["charts"] = platform != "bilibili";
    result.append(row);
  }
  return result;
}

QVariantList NativeMusicServices::accounts() const {
  QVariantList result;
  for (const auto& raw : platforms()) {
    auto row = raw.toMap();
    const auto id = row.value("id").toString();
    row["loggedIn"] = loggedIn(id);
    row["nickname"] = accounts_.value(id).user.value("nickname", "");
    result.append(row);
  }
  return result;
}

bool NativeMusicServices::loggedIn(const QString& platform) const { return !accounts_.value(platform).user.isEmpty(); }
QString NativeMusicServices::accountScope(const QString& platform) const { return accounts_.value(platform).user.value("id", "anonymous").toString(); }
QString NativeMusicServices::session(const QString& platform) const { return accounts_.value(platform).session; }
quint64 NativeMusicServices::epoch(const QString& platform) const { return accounts_.value(platform).epoch; }

QVariantMap NativeMusicServices::provider(const QString& platform, const QString& operation,
                                        QVariantMap arguments, const QString& cookie, int timeoutMs) {
  NativeServicePlatform(platform);
  QString method, backend;
  QVariantList wire;
  if (platform == "netease") {
    if (operation == "userPlaylists" && !arguments.contains("id")) arguments["id"] = accounts_.value(platform).user.value("id");
    if (operation == "search" || operation == "searchPlaylists") {
      method = "SearchNetease";
      backend = "cpp-netease-search";
      wire = {arguments.value("query"), arguments.value("page", 1), arguments.value("limit", 30), operation == "search" ? 1 : 1000, cookie};
    } else if (QStringList{"artist", "artistSongs", "artistAlbums", "album", "albumSongs"}.contains(operation)) {
      method = "NeteaseContent";
      backend = "cpp-netease-content";
      wire = {operation, arguments.value("id"), arguments.value("page", 1), arguments.value("limit", 1000), cookie};
    } else if (QStringList{"qrKey", "qrCheck", "profile", "vip", "like", "create", "addTrack", "removeTrack", "rename", "delete", "collect"}.contains(operation)) {
      method = "NeteaseAccount";
      backend = "cpp-netease-account";
      wire = {operation, json(arguments), cookie};
    } else {
      method = "NeteaseCatalog";
      backend = "cpp-netease-catalog";
      wire = {operation, arguments.value("id", "0"), operation == "stream" ? arguments.value("quality", 0) : arguments.value("offset", 0), arguments.value("limit", 100), cookie};
    }
  } else if (platform == "qq") {
    const bool qr = QStringList{"qrKey", "qrCheck", "qrCancel"}.contains(operation);
    method = qr ? "QqAccount" : "QqCatalog";
    backend = qr ? "cpp-qq-account" : "cpp-qq-catalog";
    if (QStringList{"profile", "vip", "userPlaylists"}.contains(operation) && !arguments.contains("id")) {
      arguments["id"] = accounts_.value(platform).user.value("id", qqIdentity(cookie));
    }
    wire = {operation, json(arguments), cookie};
  } else if (platform == "kugou") {
    const bool account = QStringList{"qrKey", "qrCheck", "qrCancel", "profile", "vip"}.contains(operation);
    const bool library = QStringList{"userPlaylists", "playlist", "liked", "like", "create", "collect", "share"}.contains(operation);
    const bool content = QStringList{"searchPlaylists", "stream", "qualities", "lyrics", "lyricsByInfo"}.contains(operation);
    method = account ? "KugouAccount" : library ? "KugouLibrary" : content ? "KugouContent" : "KugouCatalog";
    backend = account ? "cpp-kugou-account" : library ? "cpp-kugou-library" : content ? "cpp-kugou-content" : "cpp-kugou-catalog";
    wire = {operation, json(arguments)};
    if (method != "KugouCatalog") wire.append(cookie);
  } else {
    if (operation == "searchPlaylists") return {{"backend", "cpp-bilibili"}, {"playlists", QVariantList()}};
    const auto op = operation == "userPlaylists" ? QString("folders") : operation;
    if (op == "folders") arguments = {{"uid", accounts_.value(platform).user.value("id")},
                                     {"nickname", accounts_.value(platform).user.value("nickname", "")}};
    method = "Bilibili";
    backend = "cpp-bilibili";
    wire = {op, json(arguments), cookie};
  }
  auto result = call_ ? call_(method, wire) : busRequest(method, wire, timeoutMs);
  if (result.value("backend") != backend) throw NativeRequestFailure(QStringLiteral("平台返回的数据无效"));
  return result;
}

QVariantMap NativeMusicServices::request(const QString& platform, const QString& operation, const QVariantMap& arguments, int timeoutMs) {
  NativeServicePlatform(platform);
  const auto original = accounts_.value(platform);
  auto result = provider(platform, operation, arguments, original.session, timeoutMs);
  if (epoch(platform) != original.epoch || session(platform) != original.session) {
    throw NativeRequestFailure(QStringLiteral("请求已取消，请刷新列表"));
  }
  const auto error = result.value("error").toString();
  if (error == "login_required" || error == "login_expired") {
    logout(platform);
    throw NativeRequestFailure(QStringLiteral("登录已过期，请重新登录"), false, "auth");
  }
  if (!error.isEmpty() || (result.value("ok").type() == QVariant::Bool && !result.value("ok").toBool())) {
    const auto code = error == "vip_required" ? QString("vip") : error == "not_found" ? QString("notFound") :
        error == "unavailable" ? QString("unsupported") : QStringList{"timeout", "network_error"}.contains(error) ? QString("network") : QString("unknown");
    throw NativeRequestFailure(error == "vip_required" ? QStringLiteral("当前账号不支持该音质") : QStringLiteral("平台请求失败，请稍后重试"), false, code);
  }
  const auto updated = result.value(platform == "kugou" ? "session" : "cookie");
  if (updated.type() == QVariant::String && updated.toString() != original.session) {
    const auto cookie = NativeSessionCookie(platform, updated.toString(), original.user);
    if (!original.user.isEmpty() && NativeSessionUser(platform, cookie, original.user).isEmpty()) {
      logout(platform);
      throw NativeRequestFailure(QStringLiteral("登录资料已变化，请重新登录"));
    }
    accounts_[platform].session = cookie;
    if (loggedIn(platform)) persist(platform);
  }
  return result;
}

QVariantMap NativeMusicServices::loginRequest(const QString& platform, const QString& operation,
                                             const QVariantMap& arguments, const QString& cookie) {
  NativeServicePlatform(platform);
  if (!QStringList{"qrKey", "qrCheck", "qrCancel"}.contains(operation)) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  if (operation == "qrCancel" && (platform == "netease" || platform == "bilibili")) return {{"ok", true}};
  const auto response = provider(platform, operation, arguments, cookie, 15000);
  if (!response.value("error").toString().isEmpty() ||
      (response.value("ok").type() == QVariant::Bool && !response.value("ok").toBool() &&
       !QStringList{"expired", "failed"}.contains(response.value("qrStatus").toString()))) {
    throw NativeRequestFailure(QStringLiteral("登录接口请求失败，请重新获取二维码"));
  }
  return response;
}

void NativeMusicServices::persist(const QString& platform) {
  const auto account = accounts_.value(platform);
  try {
    storage_.saveSession(platform, NativePersistedCookie(platform, account.session), json(account.user));
  } catch (const NativeStorageError&) {
    // A failed two-file commit can leave a replayable new session on disk.
    // Retire this process's credentials until recovery establishes the pair.
    accounts_[platform] = {QString(), QVariantMap(), account.epoch + 1};
    emit accountsChanged();
    throw;
  }
}

void NativeMusicServices::acceptLogin(const QString& platform, const QString& cookie, const QVariantMap& user) {
  NativeServicePlatform(platform);
  const auto normalizedCookie = NativeSessionCookie(platform, cookie, user);
  const auto normalizedUser = NativeSessionUser(platform, normalizedCookie, user);
  if (normalizedUser.isEmpty()) throw NativeRequestFailure(QStringLiteral("登录资料不完整，请重新登录"));
  try {
    storage_.saveSession(platform, NativePersistedCookie(platform, normalizedCookie), json(normalizedUser));
  } catch (const NativeStorageError&) {
    accounts_[platform] = {QString(), QVariantMap(), epoch(platform) + 1};
    emit accountsChanged();
    throw;
  }
  auto& account = accounts_[platform];
  ++account.epoch;
  account.session = normalizedCookie;
  account.user = normalizedUser;
  emit accountsChanged();
}

void NativeMusicServices::logout(const QString& platform) {
  NativeServicePlatform(platform);
  auto& account = accounts_[platform];
  ++account.epoch;
  QString anonymous;
  if (platform == "kugou") {
    auto device = ParseNativeKugouSession(account.session);
    const QStringList keep = {"mid", "dfid", "uuid", "client"};
    QVariantMap fields;
    for (const auto& name : keep) if (device.contains(name)) fields[name] = device.value(name);
    anonymous = json(fields);
  }
  account.session = anonymous;
  account.user.clear();
  emit accountsChanged();
  storage_.saveSession(platform, QVariant(), QVariant());
}

QVariantMap NativeMusicServices::daily(const QString& platform) {
  NativeServicePlatform(platform);
  const auto originalEpoch = epoch(platform);
  const auto unchanged = [&]() {
    if (epoch(platform) != originalEpoch) throw NativeRequestFailure(QStringLiteral("请求已取消，请刷新列表"));
  };
  if (platform == "bilibili") throw NativeRequestFailure(QStringLiteral("该平台暂不提供推荐，请使用搜索或收藏夹"));
  if (platform == "netease") {
    const auto response = request(platform, "daily", {{"id", "0"}, {"limit", 1000}});
    const auto songs = NativeMusicSongs(response, platform);
    return {{"songs", songs}, {"label", songs.isEmpty() ? QStringLiteral("推荐歌曲") : QStringLiteral("每日推荐")}, {"note", ""}};
  }
  if (platform == "kugou") {
    try {
      const auto official = NativeMusicSongs(request(platform, "recommend"), platform);
      if (!official.isEmpty()) return {{"songs", official}, {"label", QStringLiteral("每日推荐")}, {"note", ""}};
    } catch (const NativeRequestFailure&) { unchanged(); }
    const auto songs = NativeMusicSongs(request(platform, "homepage"), platform).mid(0, 30);
    if (songs.isEmpty()) throw NativeRequestFailure(QStringLiteral("推荐暂时不可用，请稍后重试"));
    return {{"songs", songs}, {"label", QStringLiteral("酷狗推荐")}, {"note", QStringLiteral("官方推荐接口已不可用，来源为首页推荐")}};
  }
  const bool authenticated = !session(platform).isEmpty() || loggedIn(platform);
  if (!session(platform).isEmpty()) {
    try {
      const auto id = request(platform, "dailyId").value("dailyId").toString();
      if (!id.isEmpty()) {
        const auto songs = NativeMusicSongs(request(platform, "playlist", {{"id", id}}), platform).mid(0, 30);
        if (!songs.isEmpty()) return {{"songs", songs}, {"label", QStringLiteral("今日私享")}, {"note", ""}};
      }
    } catch (const NativeRequestFailure&) { unchanged(); }
  }
  for (const auto& chart : QList<QPair<QString, QString>>{{"27", QStringLiteral("新歌榜")}, {"26", QStringLiteral("热歌榜")}}) {
    try {
      const auto songs = NativeMusicSongs(request(platform, "ranked", {{"id", chart.first}, {"offset", 0}, {"limit", 30}, {"period", ""}}), platform);
      if (!songs.isEmpty()) return {{"songs", songs}, {"label", chart.second},
          {"note", (authenticated ? QStringLiteral("今日私享暂不可用，已回退到") : QStringLiteral("未登录，已回退到")) + chart.second}};
    } catch (const NativeRequestFailure&) { unchanged(); }
  }
  throw NativeRequestFailure(QStringLiteral("推荐暂时不可用，请稍后重试"));
}

QVariantMap NativeMusicServices::share(const QString& platform, const QString& text) {
  NativeServicePlatform(platform);
  QUrl url(text, QUrl::StrictMode);
  if (!url.isValid() || !url.userInfo().isEmpty() || !QStringList{"http", "https"}.contains(url.scheme())) throw NativeRequestFailure(QStringLiteral("歌单链接无效"));
  if (platform == "kugou" || platform == "bilibili") return request(platform, "share", {{"url", text}});
  if (!shareHost(platform, url)) throw NativeRequestFailure(QStringLiteral("歌单链接无效"));
  if (platform == "netease") {
    if (url.host() == "163cn.tv" || url.host().endsWith(".163cn.tv")) url = resolveShare(platform, url);
    const QUrlQuery query(url.fragment().isEmpty() ? url : QUrl("https://music.163.com/" + url.fragment()));
    auto id = query.queryItemValue("id");
    if (id.isEmpty()) id = QRegularExpression("(?:playlist/|playlist\\?id=)([0-9]+)").match(url.toString()).captured(1);
    if (!QRegularExpression("^[0-9]{1,32}$").match(id).hasMatch()) throw NativeRequestFailure(QStringLiteral("无法识别歌单链接，请使用完整链接"));
    return request(platform, "playlistInfo", {{"id", id}, {"limit", 1}});
  }
  auto id = qqPlaylistId(url);
  if (id.isEmpty()) {
    url = resolveShare(platform, url);
    id = qqPlaylistId(url);
  }
  if (!QRegularExpression("^[0-9]{1,18}$").match(id).hasMatch()) throw NativeRequestFailure(QStringLiteral("无法识别歌单链接，请使用完整链接"));
  return request(platform, "playlistInfo", {{"id", id}});
}

int NativeMusicServices::vipLevel(const QString& platform) {
  NativeServicePlatform(platform);
  if (!loggedIn(platform)) return 0;
  if (platform == "bilibili") return accounts_.value(platform).user.value("vipLevel").toInt();
  const auto response = request(platform, "vip");
  return qBound(0, response.value("vipLevel", accounts_.value(platform).user.value("vipLevel", 0)).toInt(), 2);
}
