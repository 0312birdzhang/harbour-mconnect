#include "native_mpris.h"

#include <QCryptographicHash>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusError>

namespace {
const QString serviceName = "org.mpris.MediaPlayer2.harbour_mconnect";
const QString objectPath = "/org/mpris/MediaPlayer2";
const QString playerInterface = "org.mpris.MediaPlayer2.Player";

class RootAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
  Q_PROPERTY(QString Identity READ identity)
  Q_PROPERTY(QString DesktopEntry READ desktopEntry)
  Q_PROPERTY(bool CanQuit READ canQuit)
  Q_PROPERTY(bool CanRaise READ canRaise)
  Q_PROPERTY(bool HasTrackList READ hasTrackList)
  Q_PROPERTY(QStringList SupportedUriSchemes READ schemes)
  Q_PROPERTY(QStringList SupportedMimeTypes READ mimeTypes)
 public:
  explicit RootAdaptor(NativeMpris& service) : QDBusAbstractAdaptor(&service), service_(service) {}
  QString identity() const { return "Mconnect"; }
  QString desktopEntry() const { return "harbour-mconnect"; }
  bool canQuit() const { return false; }
  bool canRaise() const { return service_.canRaise(); }
  bool hasTrackList() const { return false; }
  QStringList schemes() const { return {"file", "http", "https"}; }
  QStringList mimeTypes() const { return {"audio/mpeg", "audio/flac", "audio/ogg", "audio/mp4"}; }
 public slots:
  void Raise(const QDBusMessage& call) { service_.execute(call, [this]() { service_.raise(); }); }
 private:
  NativeMpris& service_;
};

class PlayerAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
  Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
  Q_PROPERTY(QVariantMap Metadata READ metadata)
  Q_PROPERTY(qlonglong Position READ position)
  Q_PROPERTY(double Rate READ rate)
  Q_PROPERTY(double MinimumRate READ rate)
  Q_PROPERTY(double MaximumRate READ rate)
  Q_PROPERTY(bool CanGoNext READ hasSong)
  Q_PROPERTY(bool CanGoPrevious READ hasSong)
  Q_PROPERTY(bool CanPlay READ hasSong)
  Q_PROPERTY(bool CanPause READ hasSong)
  Q_PROPERTY(bool CanSeek READ hasSong)
  Q_PROPERTY(bool CanControl READ canControl)
 public:
  PlayerAdaptor(NativeMpris& service, NativePlayer& player) : QDBusAbstractAdaptor(&service), service_(service), player_(player) {
    setAutoRelaySignals(false);
    connect(&service_, &NativeMpris::seeked, this, &PlayerAdaptor::Seeked);
  }
  QString playbackStatus() const { return service_.playbackStatus(); }
  QVariantMap metadata() const { return service_.metadata(); }
  qlonglong position() const { return service_.position(); }
  double rate() const { return 1; }
  bool hasSong() const { return service_.hasSong(); }
  bool canControl() const { return true; }
 public slots:
  void Play(const QDBusMessage& call) { service_.execute(call, [this]() { player_.play(); }); }
  void Pause(const QDBusMessage& call) { service_.execute(call, [this]() { player_.pause(); }); }
  void PlayPause(const QDBusMessage& call) { service_.execute(call, [this]() { player_.toggle(); }); }
  void Stop(const QDBusMessage& call) { service_.execute(call, [this]() { player_.pause(); player_.seek(0); }); }
  void Next(const QDBusMessage& call) { service_.execute(call, [this]() { player_.next(); }); }
  void Previous(const QDBusMessage& call) { service_.execute(call, [this]() { player_.previous(); }); }
  void Seek(qlonglong offset, const QDBusMessage& call) { service_.execute(call, [this, offset]() { service_.seek(offset); }); }
  void SetPosition(const QDBusObjectPath& track, qlonglong position, const QDBusMessage& call) {
    service_.execute(call, [this, track, position]() { service_.setPosition(track, position); });
  }
 signals:
  void Seeked(qlonglong position);
 private:
  NativeMpris& service_;
  NativePlayer& player_;
};
}

NativeMpris::NativeMpris(NativePlayer& player, NativeArtwork& artwork, std::function<QString()> artworkSource,
                         std::function<void()> raise, QObject* parent)
    : QObject(parent), player_(player), artwork_(artwork), artworkSource_(std::move(artworkSource)), raise_(std::move(raise)) {
  new RootAdaptor(*this);
  new PlayerAdaptor(*this, player_);
  connect(&player_, &NativePlayer::changed, this, &NativeMpris::update);
  connect(&player_, &NativePlayer::seeked, this, &NativeMpris::seeked);
  update();
}

NativeMpris::~NativeMpris() { close(); }

bool NativeMpris::start() {
  if (started_) return true;
  auto bus = QDBusConnection::sessionBus();
  if (!bus.registerObject(objectPath, this, QDBusConnection::ExportAdaptors)) return false;
  if (!bus.registerService(serviceName)) {
    bus.unregisterObject(objectPath);
    return false;
  }
  started_ = true;
  return true;
}

void NativeMpris::close() {
  if (!started_) return;
  auto bus = QDBusConnection::sessionBus();
  bus.unregisterService(serviceName);
  bus.unregisterObject(objectPath);
  started_ = false;
}

QString NativeMpris::playbackStatus() const {
  if (!hasSong()) return "Stopped";
  return player_.snapshot().value("playing").toBool() ? "Playing" : "Paused";
}

qlonglong NativeMpris::position() const { return player_.snapshot().value("position").toLongLong() * 1000; }

QDBusObjectPath NativeMpris::trackPath() const {
  if (!hasSong()) return QDBusObjectPath("/org/mpris/MediaPlayer2/TrackList/NoTrack");
  const auto hash = QCryptographicHash::hash(NativeSongKey(player_.currentSong()).toUtf8(), QCryptographicHash::Sha1).toHex();
  return QDBusObjectPath("/org/mpris/MediaPlayer2/track/t" + QString::fromLatin1(hash));
}

QVariantMap NativeMpris::metadata() const {
  QVariantMap value{{"mpris:trackid", QVariant::fromValue(trackPath())}};
  if (hasSong()) {
    const auto song = player_.currentSong();
    value["xesam:title"] = song.value("name").toString();
    value["xesam:artist"] = QStringList{NativeArtistNames(song)};
    value["mpris:length"] = player_.snapshot().value("duration").toLongLong() * 1000;
    if (!artworkUrl_.isEmpty()) value["mpris:artUrl"] = artworkUrl_;
  }
  return value;
}

QVariantMap NativeMpris::properties() const {
  return {{"PlaybackStatus", playbackStatus()}, {"Metadata", metadata()}, {"Rate", 1.0}, {"MinimumRate", 1.0}, {"MaximumRate", 1.0},
      {"CanGoNext", hasSong()}, {"CanGoPrevious", hasSong()}, {"CanPlay", hasSong()}, {"CanPause", hasSong()}, {"CanSeek", hasSong()}, {"CanControl", true}};
}

void NativeMpris::update() {
  const auto source = artworkSource_ ? artworkSource_() : player_.currentSong().value("coverUrl").toString();
  if (source != artworkSourceUrl_) {
    artworkSourceUrl_ = source;
    artworkUrl_ = artwork_.MediaUrl(source);
  }
  const auto current = properties();
  auto comparison = current;
  auto metadata = current.value("Metadata").toMap();
  metadata["mpris:trackid"] = trackPath().path();
  comparison["Metadata"] = metadata;
  QVariantMap changed;
  for (auto property = current.cbegin(); property != current.cend(); ++property) {
    if (published_.value(property.key()) != comparison.value(property.key())) changed[property.key()] = property.value();
  }
  published_ = comparison;
  if (!started_ || changed.isEmpty()) return;
  auto signal = QDBusMessage::createSignal(objectPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
  signal.setArguments({playerInterface, changed, QStringList()});
  QDBusConnection::sessionBus().send(signal);
}

void NativeMpris::raise() {
  if (!raise_) throw NativeRequestFailure(QStringLiteral("界面暂不可用"));
  raise_();
}

void NativeMpris::seek(qlonglong offset) {
  const auto current = position();
  const auto duration = player_.snapshot().value("duration").toLongLong() * 1000;
  const auto target = current + qBound(-current, offset, duration - current);
  player_.seek(target / 1000);
}

void NativeMpris::setPosition(const QDBusObjectPath& track, qlonglong value) {
  const auto duration = player_.snapshot().value("duration").toLongLong() * 1000;
  if (track == trackPath() && value >= 0 && value <= duration) player_.seek(value / 1000);
}

void NativeMpris::execute(const QDBusMessage& call, const std::function<void()>& action) {
  call.setDelayedReply(true);
  auto reply = call.createReply();
  try { action(); }
  catch (const NativeRequestFailure& error) { reply = call.createErrorReply(QDBusError::Failed, error.message); }
  catch (...) { reply = call.createErrorReply(QDBusError::Failed, QStringLiteral("播放控制失败，请稍后重试")); }
  QDBusConnection::sessionBus().send(reply);
}

#include "native_mpris.moc"
