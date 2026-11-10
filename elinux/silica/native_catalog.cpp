#include "native_platforms.h"
#include "platform_models.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>

namespace {
const QStringList operations{"playlist","playlistInfo","userPlaylists","daily","toplists","ranked","newSongs","lyrics","stream","qualities"};
QVariantMap failure(const QString& error) {return {{"backend","cpp-netease-catalog"},{"error",error}};}
QString json(const QVariantMap& value) {return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));}
bool validId(const QString& id) {return QRegularExpression("^[0-9]{1,32}$").match(id).hasMatch();}
QString identity(const QVariant& raw) {
  // Reuse the shared parser's exact-integer protection, including numeric IDs.
  return ParseNeteaseArtist({{"id",raw}}).id;
}
QVariantMap parse(const NativeHttpResult& response) {
  if(!response.error.isEmpty())return failure(response.error);
  QJsonParseError error;const auto doc=QJsonDocument::fromJson(response.body,&error);
  if(error.error!=QJsonParseError::NoError || !doc.isObject())return failure("invalid_response");
  auto root=doc.toVariant().toMap();const int code=root.value("code").toInt();
  if(code==301 || code==401)return failure("login_required");
  if(code!=200)return failure("provider_error");
  return root;
}
bool songs(const QVariant& value,QMap<QString,QVariantMap>& output,QStringList* order=nullptr) {
  if(value.type()!=QVariant::List || value.toList().size()>10000)return false;
  for(const auto& row:value.toList()) {
    if(row.type()!=QVariant::Map)return false;
    const auto song=ParseNeteaseSong(row.toMap());
    if(song.id.isEmpty() || song.name.trimmed().isEmpty())return false;
    if(order && !output.contains(song.id))order->append(song.id);
    output[song.id]=song.json();
  }
  return true;
}
class CatalogJob:public std::enable_shared_from_this<CatalogJob> {
 public:
  NativePlatformHttp* http=nullptr;QUrl endpoint;QString operation,id,cookie;
  int offset=0,limit=100,missingAt=0;QElapsedTimer elapsed;
  QMap<QString,QVariantMap> loaded;QStringList order,missing;QVariantMap metadata;
  std::function<void(QVariantMap)> done;
  void finish(QVariantMap result) {
    result["backend"]="cpp-netease-catalog";
    if(!result.contains("error")) {
      result["cookie"]=cookie;
      if(json(result).toUtf8().size()>8*1024*1024){done(failure("response_too_large"));return;}
      qInfo("MCONNECT_NATIVE_NETEASE_CATALOG_OK operation=%s",qPrintable(operation));
    }
    done(result);
  }
  void request(const QString& path,const QList<QPair<QString,QString>>& fields,bool post,std::function<void(QVariantMap)> callback) {
    const int remaining=25000-int(elapsed.elapsed());
    if(remaining<=0){finish(failure("timeout"));return;}
    QUrl url=endpoint.resolved(QUrl(path));QByteArray body;
    if(post) {for(const auto& field:fields){if(!body.isEmpty())body+='&';body+=QUrl::toPercentEncoding(field.first)+'='+QUrl::toPercentEncoding(field.second);}}
    else url=NativePlatformUrl(url,fields);
    auto req=MakeNeteaseRequest(url,cookie);if(post)req.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
    const auto self=shared_from_this();
    http->request(req,post?QByteArray("POST"):QByteArray("GET"),body,[self,callback](NativeHttpResult response){
      self->cookie=MergeNativeCookies(self->cookie,response.headers);
      callback(parse(response));
    },qMin(remaining,18000),8*1024*1024);
  }
  QString csrf()const {for(const auto& part:cookie.split(';'))if(part.trimmed().startsWith("__csrf="))return part.trimmed().mid(7);return {};}
  void assemble() {
    QVariantList result,ranks;const int start=operation=="ranked"?offset:0;
    const int end=operation=="ranked"?qMin(order.size(),offset+limit):order.size();
    for(int i=start;i<end;++i)if(loaded.contains(order[i])){result.append(loaded.value(order[i]));ranks.append(i+1);}
    finish({{"songs",result},{"ranks",ranks}});
  }
  void hydrate() {
    if(missingAt>=missing.size()){assemble();return;}
    const auto batch=missing.mid(missingAt,200);missingAt+=batch.size();
    QVariantList ids,records;for(const auto& value:batch){ids.append(value);records.append(QVariantMap{{"id",value}});}
    const auto self=shared_from_this();
    request("/api/v3/song/detail",{{"ids",QString::fromUtf8(QJsonDocument::fromVariant(ids).toJson(QJsonDocument::Compact))},{"c",QString::fromUtf8(QJsonDocument::fromVariant(records).toJson(QJsonDocument::Compact))},{"csrf",csrf()}},true,[self,batch](QVariantMap root){
      if(root.contains("error")){self->finish(root);return;}
      QMap<QString,QVariantMap> parsed;
      if(!songs(root.value("songs"),parsed)){self->finish(failure("invalid_response"));return;}
      for(const auto& key:batch)if(parsed.contains(key))self->loaded[key]=parsed.value(key);
      // Removed/unavailable IDs stay absent; preserve positions of later tracks.
      self->hydrate();
    });
  }
  void playlist(QVariantMap root) {
    if(root.contains("error")){finish(root);return;}
    const auto raw=root.value("playlist");
    if(raw.isNull()){finish(operation=="playlistInfo"?QVariantMap{{"playlists",QVariantList()}}:QVariantMap{{"songs",QVariantList()},{"ranks",QVariantList()}});return;}
    if(raw.type()!=QVariant::Map){finish(failure("invalid_response"));return;}
    const auto list=raw.toMap();
    if(operation=="playlistInfo") {
      const auto parsed=ParseNeteasePlaylist(list);
      if(parsed.id.isEmpty() || parsed.name.trimmed().isEmpty()){finish(failure("invalid_response"));return;}
      finish({{"playlists",QVariantList{parsed.json()}}});return;
    }
    if(!songs(list.value("tracks"),loaded,&order)){finish(failure("invalid_response"));return;}
    const auto rawIds=list.value("trackIds");
    if(rawIds.isValid() && rawIds.type()!=QVariant::List){finish(failure("invalid_response"));return;}
    if(rawIds.toList().size()>10000){finish(failure("response_too_large"));return;}
    if(!rawIds.toList().isEmpty()) {
      QStringList canonical;QSet<QString> seen;
      for(const auto& value:rawIds.toList()) {
        const auto key=identity(value.toMap().value("id"));
        if(!validId(key)){finish(failure("invalid_response"));return;}
        if(!seen.contains(key)){canonical.append(key);seen.insert(key);}
      }
      order=canonical;
    }
    const int wanted=operation=="ranked"?qMin(order.size(),offset+limit):order.size();
    for(int i=operation=="ranked"?offset:0;i<wanted;++i)if(!loaded.contains(order[i]))missing.append(order[i]);
    hydrate();
  }
  void newSongs(bool fallback=false) {
    const auto self=shared_from_this();
    request(fallback?"/api/personalized/newsong":"/api/v1/discovery/new/songs",fallback?QList<QPair<QString,QString>>{{"limit","100"}}:QList<QPair<QString,QString>>{{"areaId",id},{"limit","100"}},false,[self,fallback](QVariantMap root){
      if(root.value("error")=="login_required"){self->finish(root);return;}
      if(!fallback && (root.contains("error") || root.value("data").type()!=QVariant::List)) {
        if(self->id=="0"){self->newSongs(true);return;}self->finish(root.contains("error")?root:failure("invalid_response"));return;
      }
      if(root.contains("error")){self->finish(root);return;}
      QVariantList rows;
      if(fallback) {
        if(root.value("result").type()!=QVariant::List){self->finish(failure("invalid_response"));return;}
        for(const auto& value:root.value("result").toList()) {
          const auto row=value.toMap();const auto type=row.value("type");
          if(row.value("song").type()==QVariant::Map && (!type.isValid() || type==4 || type=="song"))rows.append(row.value("song"));
        }
      } else rows=root.value("data").toList();
      if(!songs(rows,self->loaded,&self->order)){self->finish(failure("invalid_response"));return;}
      self->order=self->order.mid(0,self->limit);self->assemble();
    });
  }
  void start() {
    const auto self=shared_from_this();
    if(operation=="newSongs"){newSongs();return;}
    if(operation=="playlist" || operation=="playlistInfo" || operation=="ranked") {
      request("/api/v6/playlist/detail",{{"id",id},{"n",QString::number(operation=="playlistInfo"?1:(operation=="ranked"?qMin(1000,offset+limit):1000))}},true,[self](QVariantMap root){self->playlist(root);});return;
    }
    QString path;QList<QPair<QString,QString>> fields;bool post=false;
    if(operation=="userPlaylists"){path="/api/user/playlist";post=true;fields={{"uid",id},{"limit",QString::number(limit)},{"offset",QString::number(offset)}};}
    else if(operation=="daily")path="/api/v3/discovery/recommend/songs";
    else if(operation=="toplists")path="/api/toplist";
    else if(operation=="lyrics"){path="/api/song/lyric";fields={{"id",id},{"lv","-1"},{"tv","-1"},{"yv","-1"},{"yrv","-1"},{"rv","-1"}};}
    else {path="/api/song/enhance/player/url/v1";post=true;fields={{"ids",QString::fromUtf8(QJsonDocument::fromVariant(QVariantList{id}).toJson(QJsonDocument::Compact))},{"level",operation=="qualities"?"jymaster":QStringList{"standard","higher","exhigh","lossless","hires","jyeffect","sky","jymaster"}[offset]},{"encodeType",""},{"csrf",csrf()}};}
    request(path,fields,post,[self](QVariantMap root){self->simple(root);});
  }
  void simple(const QVariantMap& root) {
    if(root.contains("error")){finish(root);return;}
    if(operation=="daily") {
      const auto rows=root.value("data").toMap().value("dailySongs");
      if(!songs(rows,loaded,&order)){finish(failure("invalid_response"));return;}assemble();return;
    }
    if(operation=="toplists" || operation=="userPlaylists") {
      const auto raw=root.value(operation=="toplists"?"list":"playlist");
      if(raw.type()!=QVariant::List || raw.toList().size()>1000){finish(failure("invalid_response"));return;}
      QVariantList lists;
      for(const auto& value:raw.toList()) {
        if(value.type()!=QVariant::Map){finish(failure("invalid_response"));return;}
        const auto row=value.toMap();auto list=ParseNeteasePlaylist(row).json();
        if(list.value("id").toString().isEmpty() || list.value("name").toString().trimmed().isEmpty())continue;
        if(operation=="toplists" && row.contains("updateFrequency"))list["updateFrequency"]=row.value("updateFrequency").toString();
        lists.append(list);
      }
      if(!raw.toList().isEmpty() && lists.isEmpty()){finish(failure("invalid_response"));return;}
      finish({{"playlists",lists}});return;
    }
    if(operation=="lyrics") {
      QVariantMap tracks;
      for(const auto& pair:QList<QPair<QString,QString>>{{"lrc","lrc"},{"translation","tlyric"},{"yrc","yrc"},{"yrcTranslation","ytlrc"},{"romaji","romalrc"}}) {
        auto value=root.value(pair.second).toMap().value("lyric");
        if(pair.first=="romaji" && value.toString().trimmed().isEmpty())value=root.value("yromalrc").toMap().value("lyric");
        if(value.isValid() && !value.isNull() && value.type()!=QVariant::String){finish(failure("invalid_response"));return;}
        if(!value.toString().trimmed().isEmpty())tracks[pair.first]=value;
      }
      finish({{"lyrics",tracks}});return;
    }
    const auto rows=root.value("data");
    if(rows.type()!=QVariant::List){finish(failure("invalid_response"));return;}
    const auto row=rows.toList().isEmpty()?QVariantMap():rows.toList().first().toMap();
    if(operation=="stream") {
      if(row.isEmpty()){finish(failure("unavailable"));return;}
      if(!row.value("freeTrialInfo").isNull()){finish(failure("vip_required"));return;}
      const auto source=row.value("url").toString();const QUrl url(source);
      if(source.isEmpty()){finish(failure(row.value("fee").toInt()>=1?"vip_required":"unavailable"));return;}
      if(!url.isValid() || (url.scheme()!="https" && url.scheme()!="http")){finish(failure("invalid_response"));return;}
      finish({{"url",source}});return;
    }
    QVariantList qualities;
    if(!row.isEmpty()) {
      const auto level=row.value("level","none").toString();const int br=row.value("br").toInt();
      const auto add=[&qualities](int value,int bitrate,const QString& format){qualities.append(QVariantMap{{"level",value},{"bitrate",bitrate},{"format",format}});};
      if(level!="none")add(0,128000,"mp3");if(br>=192000)add(1,192000,"mp3");if(br>=320000)add(2,320000,"mp3");
      if(row.value("fl").toInt()>=1000)add(3,row.value("fl").toInt(),"flac");
      const int index=QStringList{"hires","jyeffect","sky","jymaster"}.indexOf(level);
      if(index>=0)add(index+4,row.value("br",index==0 || index==3?999000:320000).toInt(),row.value("ft",index==0 || index==3?"flac":"mp3").toString());
    }
    finish({{"qualities",qualities}});
  }
};
}
QString NativePlatforms::NeteaseCatalog(const QString& operation,const QString& id,int offset,int limit,const QString& cookie) {
  bool valid=operations.contains(operation) && validId(id) && offset>=0 && offset<=10000 && limit>=1 && limit<=10000 && offset+limit<=20000 && cookie.size()<=32768;
  if(operation=="stream")valid=valid && offset<=7;
  if(operation=="newSongs")valid=valid && QStringList{"0","7","96","8","16"}.contains(id);
  for(const auto& ch:cookie)valid=valid && ch.unicode()>=32 && ch.unicode()<=126;
  if(!valid)return json(failure("invalid_arguments"));
  if(!calledFromDBus())return json(failure("dbus_required"));
  setDelayedReply(true);const auto bus=connection();const auto call=message();
  auto job=std::make_shared<CatalogJob>();job->http=&http_;job->endpoint=endpoint_;job->operation=operation;job->id=id;job->offset=offset;job->limit=limit;job->cookie=cookie;
  job->done=[bus,call](const QVariantMap& result){bus.send(call.createReply(QVariantList{json(result)}));};
  job->elapsed.start();job->start();return {};
}
