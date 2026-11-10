#include "native_runtime.h"

#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const QVariantMap ok{{"ok", true}};

QString playlistName(const QVariant& value) {
  const auto name = NativeIdentifier(value).trimmed();
  if (name.size() > 200) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  return name;
}

QString mediaPlatform(const QVariantMap& arguments) {
  const auto platform = NativeServicePlatform(arguments.value("platform"));
  if (platform == "bilibili") throw NativeRequestFailure(QStringLiteral("该平台暂不支持此资料页"));
  return platform;
}

QVariantList songRows(const QVariantList& songs) {
  QVariantList rows;
  for (const auto& song : songs) rows.append(NativeSongRow(song.toMap()));
  return rows;
}
}

NativeCoreRuntime::NativeCoreRuntime(NativeStorage& storage, QObject* parent, NativeMusicServices::ProviderCall provider,
                                   NativeDownloads::WifiProbe wifi, NativeLyricLoader::LrclibFetch lrclib)
    : QObject(parent), storage_(storage), music_(storage, this, std::move(provider)), library_(storage), results_(music_),
      recommendations_(storage, music_), downloads_(storage, music_, this, std::move(wifi)), login_(music_, this),
      local_(storage, this), lyrics_(storage, music_, this, std::move(lrclib)), player_(storage, music_, downloads_, library_, this),
      service_([this](const QString& method, const QVariantMap& arguments) { return dispatch(method, arguments); }, this) {
  requests();
  statePublish_.setInterval(500);
  connect(&statePublish_, &QTimer::timeout, this, [this]() {
    const auto state = player_.snapshot();
    if (!closed_ && state != lastState_) { lastState_ = state; service_.publish("state", state); }
  });
  connect(&player_, &NativePlayer::queueChanged, this, [this]() { if (!closed_) service_.publish("queue", songRows(player_.playlist())); });
  connect(&player_, &NativePlayer::songChanged, this, [this]() { lyrics_.load(player_.currentSong()); });
  connect(&local_, &NativeLocalMusic::changed, this, [this]() {
    if (player_.currentSong().value("platform") == "local" && !local_.snapshot().value("scanning").toBool()) lyrics_.load(player_.currentSong());
  });
  connect(&music_, &NativeMusicServices::accountsChanged, this, [this]() { if (!closed_) service_.publish("accounts", music_.accounts()); });
  connect(&lyrics_, &NativeLyricLoader::changed, this, [this]() { if (!closed_) service_.publish("lyrics", lyrics_.snapshot()); });
  connect(&login_, &NativeLogin::changed, this, [this]() {
    if (closed_) return;
    auto state = login_.state();
    state.remove("image");
    service_.publish("login", state);
  });
}

NativeCoreRuntime::~NativeCoreRuntime() { close(); }

bool NativeCoreRuntime::start() {
  if (closed_ || !service_.start()) return false;
  lastState_ = player_.snapshot();
  service_.publish("state", lastState_);
  statePublish_.start();
  service_.publish("queue", songRows(player_.playlist()));
  lyrics_.load(player_.currentSong());
  return true;
}

void NativeCoreRuntime::close() {
  if (closed_) return;
  closed_ = true;
  statePublish_.stop();
  service_.close();
  login_.cancel();
  lyrics_.close();
  player_.close();
  downloads_.close();
  local_.close();
}

QStringList NativeCoreRuntime::requestNames() const {
  auto names = requests_.keys();
  std::sort(names.begin(), names.end());
  return names;
}

QVariant NativeCoreRuntime::dispatch(const QString& method, const QVariantMap& arguments) {
  QScopedValueRollback<int> executing(executing_, executing_ + 1);
  if (closed_) throw NativeRequestFailure(QStringLiteral("请求已取消"));
  const auto request = requests_.constFind(method);
  if (request == requests_.cend()) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  return request.value()(arguments);
}

bool NativeCoreRuntime::hasOngoingWork() const {
  return !closed_ && (executing_ > 0 || service_.hasPendingWork() || player_.hasOngoingWork() ||
      downloads_.hasOngoingWork() || login_.hasOngoingWork() || local_.hasOngoingWork() || lyrics_.hasOngoingWork());
}

QVariantMap NativeCoreRuntime::song(const QVariantMap& arguments) { return results_.song(arguments, player_.currentSong()); }

QString NativeCoreRuntime::artworkSource() const {
  const auto current = player_.currentSong();
  if (!current.value("coverUrl").toString().isEmpty()) return current.value("coverUrl").toString();
  return current.value("platform") == "local" ? local_.cover(current.value("id").toString()) : QString();
}

QVariantMap NativeCoreRuntime::songInfo(const QVariantMap& arguments) {
  const auto current = song(arguments);
  const auto platform = current.value("platform").toString();
  const bool media = platform != "local" && platform != "bilibili";
  QVariantList artists;
  for (const auto& raw : current.value("artists").toList()) {
    const auto artist = raw.toMap();
    if (!artist.value("id").toString().isEmpty() && artist.value("id") != "0") {
      artists.append(QVariantMap{{"id", artist.value("id")}, {"name", artist.value("name")}});
    }
  }
  if (artists.isEmpty() && !current.value("artistId").toString().isEmpty()) artists.append(QVariantMap{{"id", current.value("artistId")}, {"name", NativeArtistNames(current)}});
  auto row = NativeSongRow(current);
  row["artists"] = artists;
  row["albumId"] = current.value("albumId").toString().isEmpty() ? current.value("album").toMap().value("id").toString() : current.value("albumId").toString();
  row["album"] = current.value("album").toMap().value("name").toString();
  return {{"song", row}, {"reference", results_.store({current}).value("token")}, {"canArtist", media}, {"canAlbum", media}, {"liked", library_.isLiked(current)}};
}

QVariantList NativeCoreRuntime::libraryPlaylists(const QVariant& source) {
  QVariantList playlists;
  if (source == "mine") playlists = storage_.playlists().playlists();
  else {
    const auto platform = NativeServicePlatform(source);
    if (!music_.loggedIn(platform)) throw NativeRequestFailure(QStringLiteral("请先登录该音乐平台"));
    playlists = music_.request(platform, "userPlaylists").value("playlists").toList();
  }
  QVariantList rows;
  for (const auto& playlist : playlists) if (!playlist.toMap().value("id").toString().isEmpty()) rows.append(NativePlaylistRow(playlist.toMap()));
  return rows;
}

QVariantMap NativeCoreRuntime::librarySongs(const QVariantMap& arguments) {
  const auto source = NativeIdentifier(arguments.value("source"));
  QVariantList songs;
  QString platform;
  if (source == "mine") songs = storage_.playlists().songs(NativeIdentifier(arguments.value("id")));
  else if (source == "likes") songs = library_.likes();
  else if (source == "history") songs = library_.history();
  else if (source == "platform") {
    platform = NativeServicePlatform(arguments.value("platform"));
    songs = NativeMusicSongs(music_.request(platform, "playlist", {{"id", NativeIdentifier(arguments.value("id"))}}), platform);
  } else throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  return results_.store(songs, platform);
}

void NativeCoreRuntime::editPlaylist(const QVariantMap& arguments, const QVariantMap& song) {
  const auto id = NativeIdentifier(arguments.value("id"));
  auto action = NativeIdentifier(arguments.value("action"));
  QVariant value;
  if (action == "rename") value = playlistName(arguments.value("name"));
  else if (action == "add" || action == "remove") {
    if (song.isEmpty()) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
    value = song;
  } else if (action == "move") {
    auto songs = storage_.playlists().songs(id);
    const int direction = NativeIntegerArgument(arguments.value("direction"), -1, 1);
    int index = -1;
    for (int position = 0; position < songs.size(); ++position) if (NativeSongKey(songs[position].toMap()) == NativeSongKey(song)) { index = position; break; }
    if (index < 0) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
    const int target = direction == 0 ? 0 : qBound(0, index + direction, songs.size() - 1);
    const auto moved = songs.takeAt(index);
    songs.insert(target, moved);
    value = songs;
    action = "reorder";
  } else if (action != "duplicate" && action != "delete") throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  if (!storage_.playlists().edit(action, id, value)) throw NativeRequestFailure(QStringLiteral("歌单不存在或内容已变化，请刷新"));
}

QVariantMap NativeCoreRuntime::discoverySongs(const QVariantMap& arguments) {
  const auto platform = NativeServicePlatform(arguments.value("platform"));
  const auto kind = NativeIdentifier(arguments.value("kind"));
  QVariantMap data;
  if (kind == "daily") {
    if (platform == "bilibili") throw NativeRequestFailure(QStringLiteral("该平台暂不提供推荐，请使用搜索或收藏夹"));
    const auto refresh = arguments.value("refresh", false);
    if (refresh.type() != QVariant::Bool) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
    data = recommendations_.get(platform, refresh.toBool());
  } else if (kind == "new") {
    if (platform == "bilibili" || platform == "kugou") throw NativeRequestFailure(QStringLiteral("该平台暂不提供新歌列表"));
    const auto region = NativeIdentifier(arguments.value("region", "all"));
    const QMap<QString, int> netease{{"all", 0}, {"chinese", 7}, {"western", 96}, {"japanese", 8}, {"korean", 16}};
    const QMap<QString, int> qq{{"all", 5}, {"chinese", 1}, {"western", 2}, {"japanese", 3}, {"korean", 4}, {"hongKongTaiwan", 6}};
    if (!qq.contains(region)) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
    if (platform == "netease" && !netease.contains(region)) throw NativeRequestFailure(QStringLiteral("新歌速递暂不提供港台地区"));
    data = {{"songs", NativeMusicSongs(music_.request(platform, "newSongs", {{"id", QString::number(platform == "netease" ? netease.value(region) : qq.value(region))}, {"limit", 100}}), platform)},
        {"label", QStringLiteral("新歌速递")}, {"note", ""}};
  } else if (kind == "chart") {
    if (platform == "bilibili") throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
    const auto period = arguments.value("period");
    if (!period.isNull() && (period.type() != QVariant::String || period.toString().size() > 128)) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
    data = {{"songs", NativeMusicSongs(music_.request(platform, "ranked", {{"id", NativeIdentifier(arguments.value("id"))}, {"offset", 0}, {"limit", 1000}, {"period", period.toString()}}), platform)},
        {"label", QStringLiteral("榜单歌曲")}, {"note", QStringLiteral("最多展示前 1000 首")}};
  } else throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  auto result = results_.store(data.value("songs").toList(), platform, kind == "daily" ? platform : QString());
  result["sourceLabel"] = data.value("label", QStringLiteral("推荐歌曲"));
  result["sourceNote"] = data.value("note", "");
  return result;
}

QVariantMap NativeCoreRuntime::importPlaylist(const QVariantMap& arguments) {
  const auto link = NativeIdentifier(arguments.value("link"), 250000).trimmed();
  const auto match = QRegularExpression("https?://[^\\s]+").match(link);
  const auto candidate = link.startsWith("mconnect:") || !match.hasMatch() ? link : match.captured();
  if (QUrl(candidate).scheme() == "mconnect") return NativePlaylistRow(storage_.playlists().importLink(candidate));
  const QUrl url(candidate, QUrl::StrictMode);
  const QMap<QString, QStringList> domains{{"netease", {"music.163.com", "163cn.tv"}}, {"qq", {"y.qq.com"}},
      {"kugou", {"kugou.com"}}, {"bilibili", {"bilibili.com", "b23.tv"}}};
  QString platform;
  for (auto item = domains.cbegin(); item != domains.cend(); ++item) for (const auto& domain : item.value()) {
    if (url.host() == domain || url.host().endsWith('.' + domain)) platform = item.key();
  }
  if (platform.isEmpty() || !QStringList{"http", "https"}.contains(url.scheme()) || !url.userInfo().isEmpty()) {
    throw NativeRequestFailure(QStringLiteral("请输入 Mconnect 或受支持平台的歌单链接"));
  }
  const auto epoch = music_.epoch(platform);
  const auto playlists = music_.share(platform, candidate).value("playlists").toList();
  if (epoch != music_.epoch(platform)) throw NativeRequestFailure(QStringLiteral("请求已取消，请刷新列表"));
  if (playlists.isEmpty()) throw NativeRequestFailure(QStringLiteral("无法识别歌单链接，请使用完整链接"));
  const auto playlist = playlists.first().toMap();
  const auto songs = NativeMusicSongs(music_.request(platform, "playlist", {{"id", playlist.value("id")}}, 12000), platform);
  if (epoch != music_.epoch(platform)) throw NativeRequestFailure(QStringLiteral("请求已取消，请刷新列表"));
  if (songs.isEmpty()) throw NativeRequestFailure(QStringLiteral("未读取到歌曲，请检查链接或登录状态"));
  return NativePlaylistRow(storage_.playlists().create(playlistName(playlist.value("name")), songs));
}

void NativeCoreRuntime::requests() {
  requests_ = {
    {"localSnapshot", [this](const QVariantMap&) { return local_.snapshot(); }},
    {"localScan", [this](const QVariantMap& args) { local_.scan(NativeIdentifier(args.value("directory"), 4096)); return QVariantMap{{"started", true}}; }},
    {"localGroups", [this](const QVariantMap& args) { return local_.groups(args); }},
    {"localSongs", [this](const QVariantMap& args) { return results_.store(local_.songs(args)); }},
    {"artistInfo", [this](const QVariantMap& args) {
      const auto artist = music_.request(mediaPlatform(args), "artist", {{"id", NativeIdentifier(args.value("id"))}}, 12000).value("artist").toMap();
      if (artist.isEmpty()) throw NativeRequestFailure(QStringLiteral("歌手资料暂不可用"));
      return QVariantMap{{"title", artist.value("name")}, {"description", artist.value("briefDesc", "")},
          {"songCount", artist.value("songCount", 0)}, {"albumCount", artist.value("albumCount", 0)}, {"albumsSupported", true}, {"artworkSource", artist.value("avatarUrl", "")}};
    }},
    {"artistSongs", [this](const QVariantMap& args) {
      const auto platform = mediaPlatform(args);
      return results_.store(NativeMusicSongs(music_.request(platform, "artistSongs", {{"id", NativeIdentifier(args.value("id"))}, {"limit", 100}}), platform), platform);
    }},
    {"artistAlbums", [this](const QVariantMap& args) {
      const auto albums = music_.request(mediaPlatform(args), "artistAlbums", {{"id", NativeIdentifier(args.value("id"))},
          {"page", NativeIntegerArgument(args.value("page", 1), 1, 100)}, {"limit", 30}}).value("albums").toList();
      QVariantList rows;
      for (const auto& album : albums) rows.append(NativeAlbumRow(album.toMap()));
      return rows;
    }},
    {"albumInfo", [this](const QVariantMap& args) {
      const auto album = music_.request(mediaPlatform(args), "album", {{"id", NativeIdentifier(args.value("id"))}}, 12000).value("album").toMap();
      if (album.isEmpty()) throw NativeRequestFailure(QStringLiteral("专辑资料暂不可用"));
      auto row = NativeAlbumRow(album);
      for (const auto& field : QStringList{"description", "company", "genre"}) row[field] = album.value(field, "");
      row["artworkSource"] = album.value("coverUrl", "");
      return row;
    }},
    {"albumSongs", [this](const QVariantMap& args) {
      const auto platform = mediaPlatform(args);
      return results_.store(NativeMusicSongs(music_.request(platform, "albumSongs", {{"id", NativeIdentifier(args.value("id"))}}), platform), platform);
    }},
    {"songInfo", [this](const QVariantMap& args) { return songInfo(args); }},
    {"resultArtwork", [this](const QVariantMap& args) { return QVariantMap{{"source", song(args).value("coverUrl").toString()}}; }},
    {"toggleLike", [this](const QVariantMap& args) { return QVariantMap{{"liked", library_.toggleLike(song(args))}}; }},
    {"addToPlaylist", [this](const QVariantMap& args) { editPlaylist({{"action", "add"}, {"id", args.value("id")}}, song(args)); return ok; }},
    {"playlistEdit", [this](const QVariantMap& args) {
      const auto action = args.value("action").toString();
      editPlaylist(args, action == "move" || action == "remove" ? song(args) : QVariantMap());
      return ok;
    }},
    {"copyResultToPlaylist", [this](const QVariantMap& args) {
      const auto songs = results_.songs(NativeIdentifier(args.value("token")));
      if (songs.isEmpty()) throw NativeRequestFailure(QStringLiteral("列表为空或已过期，请刷新"));
      return NativePlaylistRow(storage_.playlists().create(playlistName(args.value("name")), songs));
    }},
    {"importPlaylist", [this](const QVariantMap& args) { return importPlaylist(args); }},
    {"exportPlaylist", [this](const QVariantMap& args) {
      const auto link = storage_.playlists().exportLink(NativeIdentifier(args.value("id")));
      if (link.isEmpty()) throw NativeRequestFailure(QStringLiteral("歌单不存在，请刷新"));
      if (link.size() > 250000) throw NativeRequestFailure(QStringLiteral("歌单较大，请拆分后导出"));
      return QVariantMap{{"link", link}};
    }},
    {"discoveryPlatforms", [this](const QVariantMap&) { return music_.discoveryPlatforms(); }},
    {"discoveryCharts", [this](const QVariantMap& args) {
      const auto platform = NativeServicePlatform(args.value("platform"));
      if (platform == "bilibili") throw NativeRequestFailure(QStringLiteral("该平台暂不提供榜单，请使用搜索或收藏夹"));
      const auto result = music_.request(platform, "toplists");
      QVariantList rows;
      for (const auto& raw : result.value("charts", result.value("playlists")).toList()) {
        const auto chart = raw.toMap();
        rows.append(QVariantMap{{"id", chart.value("id")}, {"title", chart.value("name")}, {"period", chart.value("period", "")},
            {"group", chart.value("groupName", "")}, {"description", chart.value("updateFrequency", "")}});
      }
      return rows;
    }},
    {"discoverySongs", [this](const QVariantMap& args) { return discoverySongs(args); }},
    {"downloads", [this](const QVariantMap&) { return downloads_.snapshot(); }},
    {"downloadDirectory", [this](const QVariantMap&) { return QVariantMap{{"path", downloads_.directory()}}; }},
    {"setDownloadDirectory", [this](const QVariantMap& args) {
      downloads_.setDirectory(NativeIdentifier(args.value("path"), 4096));
      return QVariantMap{{"path", downloads_.directory()}};
    }},
    {"downloadControl", [this](const QVariantMap& args) { downloads_.control(args); return ok; }},
    {"downloadEnqueue", [this](const QVariantMap& args) { downloads_.enqueue(song(args), NativeQualityIndex(args.value("quality"))); return ok; }},
    {"playDownload", [this](const QVariantMap& args) { player_.playSong(downloads_.taskSong(NativeIdentifier(args.value("id"), 8192))); return ok; }},
    {"cacheSettings", [this](const QVariantMap&) { return storage_.cacheSettings(); }},
    {"setCacheSetting", [this](const QVariantMap& args) {
      const auto key = NativeIdentifier(args.value("key"));
      const auto value = args.value("value");
      const auto settings = storage_.cacheSettings();
      if (!settings.contains(key)) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
      if (key == "sizeLimitMb") NativeIntegerArgument(value, 128, 32768);
      else if (value.type() != QVariant::Bool) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
      const auto result = storage_.setCacheSetting(key, value);
      downloads_.policyChanged();
      return result;
    }},
    {"toggleShuffle", [this](const QVariantMap&) { player_.toggleShuffle(); return ok; }},
    {"cycleRepeat", [this](const QVariantMap&) { player_.cycleRepeat(); return ok; }},
    {"accounts", [this](const QVariantMap&) { return music_.accounts(); }},
    {"loginBegin", [this](const QVariantMap& args) {
      const auto variant = args.value("variant");
      if (!variant.isNull() && variant.type() != QVariant::String) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
      return login_.begin(NativeServicePlatform(args.value("platform")), NativeIdentifier(args.value("session"), 128), variant.toString());
    }},
    {"loginStatus", [this](const QVariantMap&) { return login_.state(); }},
    {"loginCancel", [this](const QVariantMap& args) { login_.cancel(args.value("session")); return ok; }},
    {"logout", [this](const QVariantMap& args) {
      const auto platform = NativeServicePlatform(args.value("platform"));
      if (login_.state().value("platform") == platform) login_.cancel();
      music_.logout(platform);
      return ok;
    }},
    {"libraryPlaylists", [this](const QVariantMap& args) { return libraryPlaylists(args.value("source")); }},
    {"librarySongs", [this](const QVariantMap& args) { return librarySongs(args); }},
    {"resultPage", [this](const QVariantMap& args) { return results_.page(NativeIdentifier(args.value("token")), NativeIntegerArgument(args.value("offset"), 0, std::numeric_limits<int>::max())); }},
    {"createPlaylist", [this](const QVariantMap& args) { return NativePlaylistRow(storage_.playlists().create(playlistName(args.value("name")))); }},
    {"addCurrentToPlaylist", [this](const QVariantMap& args) { editPlaylist({{"action", "add"}, {"id", args.value("id")}}, song({{"current", true}})); return ok; }},
    {"snapshot", [this](const QVariantMap&) { return player_.snapshot(); }},
    {"platforms", [this](const QVariantMap&) { return music_.platforms(); }},
    {"queue", [this](const QVariantMap&) { return songRows(player_.playlist()); }},
    {"lyrics", [this](const QVariantMap&) { return lyrics_.snapshot(); }},
    {"artwork", [this](const QVariantMap&) { return QVariantMap{{"source", artworkSource()}}; }},
    {"search", [this](const QVariantMap& args) {
      const auto platform = NativeServicePlatform(args.value("platform"));
      const auto query = NativeIdentifier(args.value("query"), 256).trimmed();
      const auto page = NativeIntegerArgument(args.value("page", 1), 1, 100);
      return results_.store(NativeMusicSongs(music_.request(platform, "search", {{"query", query}, {"page", page}, {"limit", 30}}), platform), platform);
    }},
    {"searchPlaylists", [this](const QVariantMap& args) {
      const auto platform = NativeServicePlatform(args.value("platform"));
      const auto result = music_.request(platform, "searchPlaylists", {{"query", NativeIdentifier(args.value("query"), 256).trimmed()},
          {"page", NativeIntegerArgument(args.value("page", 1), 1, 100)}, {"limit", 30}});
      QVariantList rows;
      for (const auto& playlist : result.value("playlists").toList()) rows.append(NativePlaylistRow(playlist.toMap()));
      return QVariantMap{{"playlists", rows}};
    }},
    {"playResult", [this](const QVariantMap& args) {
      const auto songs = results_.songs(NativeIdentifier(args.value("token")));
      player_.playPlaylist(songs, NativeIntegerArgument(args.value("index"), 0, songs.size() - 1));
      return true;
    }},
    {"toggle", [this](const QVariantMap&) { player_.toggle(); return true; }},
    {"next", [this](const QVariantMap&) { player_.next(); return true; }},
    {"previous", [this](const QVariantMap&) { player_.previous(); return true; }},
    {"seek", [this](const QVariantMap& args) {
      const auto value = args.value("position");
      bool valid = false;
      const auto position = value.toLongLong(&valid);
      if (!valid || value.type() == QVariant::String || value.type() == QVariant::Bool ||
          (value.type() == QVariant::Double && (!std::isfinite(value.toDouble()) || value.toDouble() != double(position)))) {
        throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
      }
      player_.seek(position);
      return true;
    }},
    {"playIndex", [this](const QVariantMap& args) { player_.playIndex(NativeIntegerArgument(args.value("index"), 0, player_.playlist().size() - 1)); return true; }}
  };
}
