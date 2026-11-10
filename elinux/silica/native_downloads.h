#pragma once

#include "native_library.h"
#include <QNetworkAccessManager>
#include <QTimer>

bool NativeWifiDefaultRoute(const QString& ipv4Path = "/proc/net/route", const QString& ipv6Path = "/proc/net/ipv6_route",
                            const QString& networkRoot = "/sys/class/net");
QString NativeDownloadFileName(const QVariantMap& song, int quality);

class NativeDownloads : public QObject {
  Q_OBJECT
 public:
  using WifiProbe = std::function<bool()>;
  using SpaceProbe = std::function<qint64(const QString&)>;
  NativeDownloads(NativeStorage& storage, NativeMusicServices& music, QObject* parent = nullptr,
                  WifiProbe wifi = {}, SpaceProbe space = {}, bool resumeStored = true);
  ~NativeDownloads() override;
  QVariantMap snapshot() const;
  bool hasOngoingWork() const {
    return !closed_ && (pumping_ || !jobs_.isEmpty() || flushTimer_.isActive() ||
        (!paused_ && (!waiting_.isEmpty() || !retryTimers_.isEmpty())));
  }
  QString directory() const;
  void setDirectory(const QString& path);
  void enqueue(const QVariantMap& song, int quality, bool cache = false);
  void control(const QVariantMap& arguments);
  QVariantMap taskSong(const QString& id) const;
  QString localFile(const QVariantMap& song, int quality = -1);
  void policyChanged();
  void cleanup();
  void close();
 signals:
  void changed();
 private:
  struct Job;
  NativeStorage& storage_;
  NativeMusicServices& music_;
  QNetworkAccessManager network_;
  WifiProbe wifi_;
  SpaceProbe space_;
  QMap<QString, QVariantMap> tasks_;
  QStringList order_, waiting_;
  QHash<QString, std::shared_ptr<Job>> jobs_;
  QHash<QString, int> retries_;
  QHash<QString, QTimer*> retryTimers_;
  QTimer flushTimer_, connectionTimer_;
  bool paused_ = false, pumping_ = false, closed_ = false, storageFailed_ = false;
  void persist(bool immediate = true);
  void pump();
  void start(const QString& id);
  void send(const std::shared_ptr<Job>& job);
  bool prepare(const std::shared_ptr<Job>& job);
  void read(const std::shared_ptr<Job>& job);
  void finish(const std::shared_ptr<Job>& job, const QString& failure = {});
  void stop(const QString& id, bool remove = false);
  void scheduleRetry(const QString& id, const QString& failure);
  QString pathFor(const QVariantMap& task) const;
  QString blocked() const;
  bool sharedPath(const QString& path, const QString& except) const;
};
