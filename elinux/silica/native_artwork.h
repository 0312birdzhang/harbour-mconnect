#pragma once
#include <QObject>
#include <QCache>
#include <QHash>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QSet>
#include <QThread>
#include <functional>
#include <memory>
class QElapsedTimer;

QByteArray EncodeNativeArtwork(const QByteArray& bytes);
QByteArray DecodeNativeArtwork(const QByteArray& bytes);

class NativeArtwork : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.sailfishos.Mconnect.Artwork")
 public:
  explicit NativeArtwork(QObject* parent=nullptr, const QString& cacheDirectory=QString());
  ~NativeArtwork() override;
 public slots:
  QString MediaUrl(const QString& source);
 private:
  using Done=std::function<void(QByteArray)>;
  void load(const QString& source, Done done);
  void download(const QString& source, const QUrl& url, int redirects, Done done,
                std::shared_ptr<QElapsedTimer> deadline=nullptr);
  void finish(const QString& source,const QByteArray& bytes);
  void accept();
  void startJob(QThread* job);
  QTcpServer server_;
  QNetworkAccessManager network_;
  QString directory_, token_;
  QHash<QString,QString> routes_;
  QList<QString> routeOrder_;
  QCache<QString,QByteArray> memory_{32*1024};
  QHash<QString,QList<Done>> pending_;
  QSet<QThread*> jobs_;
};
