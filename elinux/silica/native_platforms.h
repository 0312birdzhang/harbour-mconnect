#pragma once
#include <QObject>
#include <QDBusContext>
#include <QVariantMap>
#include <QElapsedTimer>
#include "platform_http.h"

QVariantMap ParseNeteaseSearch(const QByteArray& body,int type);
QString MergeNativeCookies(const QString& original,const QList<QPair<QByteArray,QByteArray>>& headers);
QNetworkRequest MakeNeteaseRequest(const QUrl& url,const QString& cookie);
QVariantMap ParseNeteaseContent(const QString& operation,const QByteArray& body,bool legacy=false);
QVariantMap NormalizeQqProfile(const QVariantMap& data,const QString& uin);
QVariantMap ParseNativeQqResponse(QByteArray body);
QVariantMap ParseNativeKugouResponse(QByteArray body);
QVariantMap ParseNativeKugouSession(const QString& encoded);
QVariantMap ParseNativeKugouSong(const QVariantMap& data);
QVariantMap ParseNativeKugouPlaylist(const QVariantMap& data, bool editable = false);
bool NativeKugouSuccess(const QVariantMap& data);
enum class NativeKugouSigning { Android, Lite, Web, H5 };
QVariantMap NativeKugouParameters(const QVariantMap& session, const QVariantMap& parameters,
    const QByteArray& body = {}, NativeKugouSigning signing = NativeKugouSigning::Android);

struct NativeBiliState { QString mixin; qint64 timestamp=0; };
struct NativeQqAuthState {
  QString key;
  QString cookie;
  QElapsedTimer lifetime;
  bool busy=false;
  QString terminalCookie;
  QVariantMap terminalResult;
};
struct NativeKugouAuthState {
  QString key, providerKey;
  QVariantMap session, requestSession, terminalResult;
  QElapsedTimer lifetime;
  bool busy = false;
};
QMap<QString,QString> NativeBiliSign(QMap<QString,QString> parameters,const QString& mixin,qint64 timestamp);
QString NativeBiliMixin(const QString& image,const QString& sub);

class NativePlatforms : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.sailfishos.Mconnect.Platforms")
 public:
  explicit NativePlatforms(QObject* parent=nullptr,const QUrl& endpoint=QUrl("https://music.163.com"),
    const QUrl& bili=QUrl("https://api.bilibili.com"),const QUrl& passport=QUrl("https://passport.bilibili.com"),
    const QUrl& qq=QUrl("https://u.y.qq.com"),const QUrl& qqLegacy=QUrl("https://c.y.qq.com"),const QUrl& kugou=QUrl(),const QUrl& qqAuth=QUrl())
    :QObject(parent),http_(this),endpoint_(endpoint),biliEndpoint_(bili),biliPassport_(passport),qqEndpoint_(qq),qqLegacy_(qqLegacy),kugouEndpoint_(kugou),qqAuthEndpoint_(qqAuth) {}
 public slots:
  QString SearchNetease(const QString& query,int page,int limit,int type,const QString& cookie);
  QString NeteaseContent(const QString& operation,const QString& id,int page,int limit,const QString& cookie);
  QString NeteaseCatalog(const QString& operation,const QString& id,int offset,int limit,const QString& cookie);
  QString NeteaseAccount(const QString& operation,const QString& arguments,const QString& cookie);
  QString Bilibili(const QString& operation,const QString& arguments,const QString& cookie);
  QString QqCatalog(const QString& operation,const QString& arguments,const QString& cookie);
  QString QqAccount(const QString& operation,const QString& arguments,const QString& cookie);
  QString KugouCatalog(const QString& operation,const QString& arguments);
  QString KugouContent(const QString& operation,const QString& arguments,const QString& session);
  QString KugouAccount(const QString& operation,const QString& arguments,const QString& session);
  QString KugouLibrary(const QString& operation,const QString& arguments,const QString& session);
 private:
  NativePlatformHttp http_;
  QUrl endpoint_;
  QUrl biliEndpoint_,biliPassport_;
  QUrl qqEndpoint_,qqLegacy_;
  QUrl kugouEndpoint_;
  QUrl qqAuthEndpoint_;
  QString qqGuid_;
  std::shared_ptr<NativeBiliState> biliState_=std::make_shared<NativeBiliState>();
  std::shared_ptr<NativeQqAuthState> qqAuthState_=std::make_shared<NativeQqAuthState>();
  std::shared_ptr<NativeKugouAuthState> kugouAuthState_=std::make_shared<NativeKugouAuthState>();
};
