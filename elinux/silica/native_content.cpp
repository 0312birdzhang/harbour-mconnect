#include "native_platforms.h"
#include "platform_models.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <algorithm>

namespace {
const QStringList operations{"artist","artistSongs","artistAlbums","album","albumSongs"};
QVariantMap failure(const QString& error) {return {{"error",error},{"backend","cpp-netease-content"}};}
QString json(const QVariantMap& data) {return QString::fromUtf8(QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact));}
QVariantMap empty(const QString& operation) {
  QVariantMap result{{"backend","cpp-netease-content"}};
  if(operation=="artist" || operation=="album")result[operation]=QVariant();
  else result[operation=="artistAlbums"?"albums":"songs"]=QVariantList();
  return result;
}
class ContentJob: public std::enable_shared_from_this<ContentJob> {
 public:
  NativePlatformHttp* http=nullptr;
  QUrl endpoint;
  QString operation,id,cookie;
  int page=1,limit=30;
  bool answered=false;
  QElapsedTimer elapsed;
  QVariantMap firstError;
  std::function<void(QVariantMap)> done;
  void finish(QVariantMap result) {
    if(!result.contains("error")) {
      const QString field=operation=="artistAlbums"?"albums":"songs";
      if(operation=="artistAlbums" || operation=="artistSongs")result[field]=result.value(field).toList().mid(0,limit);
      result["cookie"]=cookie;
      qInfo("MCONNECT_NATIVE_NETEASE_CONTENT_OK operation=%s",qPrintable(operation));
    }
    done(result);
  }
  void fetch(bool legacy=false) {
    const int remaining=10000-int(elapsed.elapsed());
    const bool fallback=operation=="artist" || operation=="artistSongs";
    if(remaining<=0){finish(answered?empty(operation):(firstError.isEmpty()?failure("timeout"):firstError));return;}
    QString path;QList<QPair<QString,QString>> query;QByteArray method="GET",body;
    if(legacy)path="/api/artist/"+id;
    else if(operation=="artist"){path="/api/artist/head/info/get";method="POST";body="id="+QUrl::toPercentEncoding(id);}
    else if(operation=="artistSongs"){path="/api/artist/top/song";query={{"id",id}};}
    else if(operation=="artistAlbums"){path="/api/artist/albums/"+id;query={{"limit",QString::number(limit)},{"offset",QString::number((page-1)*limit)}};}
    else path="/api/v1/album/"+id;
    auto request=MakeNeteaseRequest(NativePlatformUrl(endpoint.resolved(QUrl(path)),query),cookie);
    if(method=="POST")request.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
    const auto self=shared_from_this();
    http->request(request,method,body,[self,legacy,fallback](NativeHttpResult response){
      self->cookie=MergeNativeCookies(self->cookie,response.headers);
      const auto result=response.error.isEmpty()?ParseNeteaseContent(self->operation,response.body,legacy)
        :(response.status==404?empty(self->operation):failure(response.error));
      if(!result.contains("error")) {
        self->answered=true;
        const bool missing=self->operation=="artist"?result.value("artist").isNull():result.value("songs").toList().isEmpty();
        if(fallback && !legacy && missing){self->fetch(true);return;}
        self->finish(result);return;
      }
      if(result.value("error")=="login_required"){self->finish(result);return;}
      if(self->firstError.isEmpty())self->firstError=result;
      if(fallback && !legacy){self->fetch(true);return;}
      self->finish(self->answered?empty(self->operation):self->firstError);
    },fallback && !legacy?std::min(remaining,5000):remaining);
  }
};
}
QVariantMap ParseNeteaseContent(const QString& operation,const QByteArray& body,bool legacy) {
  if(!operations.contains(operation))return failure("invalid_arguments");
  QJsonParseError error;const auto document=QJsonDocument::fromJson(body,&error);
  if(error.error!=QJsonParseError::NoError || !document.isObject())return failure("invalid_response");
  const auto root=document.toVariant().toMap();const int code=root.value("code").toInt();
  if(code==301 || code==401)return failure("login_required");
  if(code==404)return empty(operation);
  if(code!=200)return failure("provider_error");
  auto result=empty(operation);
  if(operation=="artist" || operation=="album") {
    const auto data=root.value("data");
    if(operation=="artist" && !legacy && data.isValid() && !data.isNull() && data.type()!=QVariant::Map)return failure("invalid_response");
    const auto raw=operation=="artist"?(legacy?root.value("artist"):root.value("data").toMap().value("artist")):root.value("album");
    if(!raw.isValid() || raw.isNull())return result;
    if(raw.type()!=QVariant::Map)return failure("invalid_response");
    const auto parsed=operation=="artist"?ParseNeteaseArtist(raw.toMap()).json():ParseNeteaseAlbum(raw.toMap()).json();
    if(parsed.value("id").toString().isEmpty() || parsed.value("name").toString().trimmed().isEmpty())return failure("invalid_response");
    result[operation]=parsed;return result;
  }
  const QString field=operation=="artistAlbums"?"hotAlbums":(legacy?"hotSongs":"songs");
  const auto raw=root.value(field);
  if(!raw.isValid() || raw.isNull())return result;
  if(raw.type()!=QVariant::List || raw.toList().size()>2000)return failure("invalid_response");
  QVariantList entries;
  bool allNumbered=true;
  for(const auto& value:raw.toList()) {
    if(value.type()!=QVariant::Map)continue;
    if(operation=="artistAlbums") {
      const auto album=ParseNeteaseAlbum(value.toMap());if(!album.id.isEmpty() && !album.name.trimmed().isEmpty())entries.append(album.json());
    } else {
      const auto song=ParseNeteaseSong(value.toMap());if(song.id.isEmpty() || song.name.trimmed().isEmpty())continue;
      allNumbered=allNumbered && song.trackNumber.isValid();entries.append(song.json());
    }
  }
  if(!raw.toList().isEmpty() && entries.isEmpty())return failure("invalid_response");
  if(operation=="albumSongs" && allNumbered)std::stable_sort(entries.begin(),entries.end(),[](const QVariant& a,const QVariant& b){return a.toMap().value("trackNumber").toLongLong()<b.toMap().value("trackNumber").toLongLong();});
  result[operation=="artistAlbums"?"albums":"songs"]=entries;return result;
}
QString NativePlatforms::NeteaseContent(const QString& operation,const QString& id,int page,int limit,const QString& cookie) {
  if(!operations.contains(operation) || !QRegularExpression("^[0-9]{1,32}$").match(id).hasMatch() || page<1 || page>100 || limit<1 || limit>1000 || cookie.size()>32768)return json(failure("invalid_arguments"));
  for(const auto ch:cookie)if(ch.unicode()<32 || ch.unicode()>126)return json(failure("invalid_arguments"));
  if(!calledFromDBus())return json(failure("dbus_required"));
  setDelayedReply(true);const auto bus=connection();const auto call=message();
  auto job=std::make_shared<ContentJob>();job->http=&http_;job->endpoint=endpoint_;job->operation=operation;job->id=id;job->page=page;job->limit=limit;job->cookie=cookie;
  job->done=[bus,call](const QVariantMap& result){bus.send(call.createReply(QVariantList{json(result)}));};
  job->elapsed.start();job->fetch();return {};
}
