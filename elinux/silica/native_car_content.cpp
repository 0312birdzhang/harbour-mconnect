#include "native_car_content.h"
#include "sailplaycontent.h"

#include <QCryptographicHash>

NativeCarContent::NativeCarContent(SailplayProvider& provider, QObject* parent)
    : QObject(parent), provider_(provider) {
  const auto browse = [this](qulonglong invocation, const QVariantMap& parameters) {
    if (!player_) { provider_.fail(invocation, "Unavailable", "Music core is starting"); return; }
    const auto page = this->browse(parameters);
    if (page.isEmpty()) { provider_.fail(invocation, "NotFound", "Content folder is unavailable"); return; }
    provider_.complete(invocation, {{"content", page}});
  };
  provider_.setActionHandler("content.browse", browse);
  provider_.setActionHandler("content.search", browse);
  provider_.setActionHandler("content.selectSong", [this](qulonglong invocation, const QVariantMap& parameters) {
    if (!select(parameters)) { provider_.fail(invocation, "NotFound", "Queue item has changed"); return; }
    provider_.complete(invocation, {{"selected", true}});
  });
}

void NativeCarContent::attach(NativePlayer& player) {
  player_ = &player;
  connect(&player, &NativePlayer::queueChanged, this, [this]() { ++version_; });
}

QString NativeCarContent::songKey(const QVariantMap& song) const {
  QByteArray bytes = song.value("platform").toString().toUtf8();
  bytes.append('\0');
  bytes.append(song.value("id").toString().toUtf8());
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QVariantMap NativeCarContent::root() const {
  auto page = SailplayContent::page("root", tr("Mconnect"));
  page["version"] = version_;
  page["searchEnabled"] = true;
  auto queue = SailplayContent::item("queue", tr("Playback queue"));
  queue["subtitle"] = tr("%n song(s)", nullptr, player_ ? player_->playlist().size() : 0);
  page["items"] = QVariantList{queue};
  if (!player_) {
    page["message"] = tr("Music core is starting");
    page["messageKind"] = "info";
  }
  return page;
}

QVariantMap NativeCarContent::browse(const QVariantMap& parameters) const {
  const bool search = parameters.contains("query");
  if (!player_ || !SailplayContent::validQuery(parameters, search)) return {};
  const auto identity = parameters.value("id").toString();
  if (identity != "root" && identity != "queue") return {};
  if (identity == "root" && !search) {
    auto page = root();
    const uint offset = parameters.value("offset").toUInt();
    if (offset) page["items"] = QVariantList();
    page["offset"] = offset;
    return page;
  }
  const auto query = parameters.value("query").toString();
  const auto songs = player_->playlist();
  const quint64 offset = parameters.value("offset").toUInt();
  const quint64 limit = parameters.value("limit").toUInt();
  QVariantList items;
  quint64 matched = 0;
  for (int index = 0; index < songs.size(); ++index) {
    const auto song = songs[index].toMap();
    QStringList artists;
    for (const auto& artist : song.value("artists").toList()) artists.append(artist.toMap().value("name").toString());
    const auto title = song.value("name").toString();
    const auto subtitle = artists.join(", ");
    if (search && !title.contains(query, Qt::CaseInsensitive) && !subtitle.contains(query, Qt::CaseInsensitive)) continue;
    if (matched >= offset && matched < offset + limit) {
      const auto key = songKey(song);
      auto item = SailplayContent::item(QString::number(index) + "-" + key,
          title.isEmpty() ? tr("Unknown song") : title.left(1024), "action", "content.selectSong",
          {{"index", uint(index)}, {"key", key}});
      item["subtitle"] = subtitle.left(1024);
      items.append(item);
    }
    ++matched;
  }
  auto page = SailplayContent::page(identity, search ? tr("Queue search") : tr("Playback queue"));
  page["version"] = version_;
  page["items"] = items;
  page["searchEnabled"] = true;
  page["offset"] = uint(offset);
  page["hasMore"] = offset + items.size() < matched;
  if (!matched) {
    page["message"] = search ? tr("No matching songs") : tr("Playback queue is empty");
    page["messageKind"] = "info";
  }
  return page;
}

bool NativeCarContent::select(const QVariantMap& parameters) {
  if (!player_ || parameters.keys() != QStringList{"index", "key"}) return false;
  const auto value = parameters.value("index");
  if (value.type() != QVariant::UInt || parameters.value("key").type() != QVariant::String) return false;
  const auto index = value.toUInt();
  const auto songs = player_->playlist();
  if (index >= uint(songs.size()) || songKey(songs[index].toMap()) != parameters.value("key").toString()) return false;
  player_->playIndex(int(index));
  return true;
}
