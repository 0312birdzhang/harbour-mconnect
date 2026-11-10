#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <functional>
#include <memory>

QUrl NativePlatformUrl(const QUrl& base,const QList<QPair<QString,QString>>& query);

struct NativeHttpResult {
  QByteArray body;
  QList<QPair<QByteArray,QByteArray>> headers;
  int status=0;
  QString error;
};
class NativePlatformHttp : public QObject {
 public:
  using Done=std::function<void(NativeHttpResult)>;
  explicit NativePlatformHttp(QObject* parent=nullptr);
  ~NativePlatformHttp() override;
  void request(const QNetworkRequest& request,const QByteArray& method,const QByteArray& body,Done done,
               int timeoutMs=18000,int maxBytes=4*1024*1024,bool safeGet=true,bool publicKugou=false,
               bool followRedirects=true,bool legacyKugouAccount=false);
 private:
  struct Pending;
  void send(std::shared_ptr<Pending> pending);
  QNetworkAccessManager network_;
};
