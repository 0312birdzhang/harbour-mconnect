#include "native_platforms.h"
#include "platform_models.h"
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

namespace {
const QStringList operations{"search","artist","artistSongs","artistAlbums","album","albumSongs","toplists","ranked","homepage","recommend"};
QVariantMap fail(const QString& reason){return {{"backend","cpp-kugou-catalog"},{"error",reason}};}
QString json(const QVariantMap& data){return QString::fromUtf8(QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact));}
qint64 number(const QVariant& value,qint64 fallback=-1){bool ok=false;const auto n=value.toLongLong(&ok);return ok && value.type()!=QVariant::Bool && (value.type()!=QVariant::Double || value.toDouble()==double(n))?n:fallback;}
QString identity(const QVariant& value){return ParseNeteaseArtist({{"id",value}}).id;}
QString text(const QVariantMap& row,const QStringList& keys){for(const auto& key:keys){const auto v=row.value(key);if(v.type()==QVariant::Bool || v.isNull())continue;const auto s=v.type()==QVariant::String?v.toString().trimmed():identity(v);if(!s.isEmpty())return s;}return {};}
QString clean(QString s){s.remove(QRegularExpression("<[^>]*>"));return s.trimmed();}
QString image(QString value){value.replace("{size}","480");if(value.startsWith("//"))value="https:"+value;const QUrl url(value);return (url.scheme()=="http" || url.scheme()=="https") && !url.host().isEmpty()?value:QString();}
QVariantMap song(const QVariantMap& raw){
  NativeSong s;s.platform="kugou";s.id=text(raw,{"hash"});if(!QRegularExpression("^[a-fA-F0-9]{32}$").match(s.id).hasMatch())return {};
  s.name=clean(text(raw,{"songname","song_name","name"}));auto singer=clean(text(raw,{"singername","singer_name"}));const auto info=raw.value("singerinfo").toList();
  if(singer.isEmpty() && !info.isEmpty())singer=clean(text(info.first().toMap(),{"name","singername"}));const auto filename=clean(text(raw,{"filename","name"}));
  if((s.name.isEmpty() || singer.isEmpty()) && filename.contains(" - ")){const int split=filename.indexOf(" - ");if(singer.isEmpty())singer=filename.left(split).trimmed();if(s.name.isEmpty())s.name=filename.mid(split+3).trimmed();}
  if(s.name.contains(" - ") && (singer.isEmpty() || s.name.startsWith(singer+" - "))){const int split=s.name.indexOf(" - ");if(singer.isEmpty())singer=s.name.left(split).trimmed();s.name=s.name.mid(split+3).trimmed();}
  if(s.name.isEmpty())s.name=filename;if(s.name.isEmpty())return {};
  for(const auto& value:raw.value("authors").toList()){const auto author=value.toMap();const auto name=clean(text(author,{"author_name","name"}));if(!name.isEmpty())s.artists.append({text(author,{"author_id","id"}),name});}
  if(s.artists.isEmpty() && !singer.isEmpty())s.artists.append({text(raw,{"singerid"}),singer});
  s.coverUrl=image(text(raw.value("trans_param").toMap(),{"union_cover"}));if(s.coverUrl.isEmpty())s.coverUrl=image(text(raw,{"album_img","image","cover","album_sizable_cover"}));
  const auto albumName=text(raw,{"album_name"});if(!albumName.isEmpty()){s.hasAlbum=true;s.album={text(raw,{"album_id"}),albumName,{}};}
  const auto ms=number(raw.value("timelen")),seconds=number(raw.value("duration",raw.value("timeLength")));s.durationMs=ms>0?ms:seconds>0?seconds*1000:0;if(s.durationMs>604800000)s.durationMs=0;return s.json();
}
QVariantMap album(const QVariantMap& raw){
  NativeAlbum a;a.id=text(raw,{"albumid"});a.name=text(raw,{"albumname"});if(a.id.isEmpty() || a.name.isEmpty())return {};
  a.artistId=text(raw,{"singerid"});a.artistName=text(raw,{"singername"});a.coverUrl=image(text(raw,{"imgurl","album_sizable_cover"}));a.description=text(raw,{"intro"});const auto n=number(raw.value("songcount"));if(n>=0)a.songCount=n;
  auto date=text(raw,{"publishtime"});date.replace(' ','T');auto parsed=QDateTime::fromString(date,Qt::ISODate);if(!parsed.isValid() && QDate::fromString(date,"yyyy-MM-dd").isValid())date+="T00:00:00";if(QDateTime::fromString(date,Qt::ISODate).isValid())a.releaseDate=date;return a.json();
}
QVariantMap parse(QByteArray body){
  auto value=QString::fromUtf8(body.constData(),body.size());if(value.toUtf8()!=body)return fail("invalid_response");value.replace("<!--KG_TAG_RES_START-->","");value.replace("<!--KG_TAG_RES_END-->","");body=value.trimmed().toUtf8();
  for(int i=0;i<3;++i){QJsonParseError error;auto doc=QJsonDocument::fromJson(body,&error);if(error.error==QJsonParseError::NoError && doc.isObject()){auto root=doc.toVariant().toMap();if(root.contains("status") && number(root.value("status"))!=1)return fail("provider_error");if(!root.contains("status") && !root.value("error").toString().isEmpty())return fail("provider_error");root.remove("error");return root;}doc=QJsonDocument::fromJson('['+body+']',&error);if(error.error!=QJsonParseError::NoError || !doc.isArray() || doc.array().size()!=1 || !doc.array().first().isString())return fail("invalid_response");body=doc.array().first().toString().toUtf8();}return fail("invalid_response");
}
bool valid(const QString& op,const QVariantMap& args){
  QSet<QString> fields;if(op=="search")fields={"query","page","limit"};else if(op=="artistAlbums")fields={"id","page","limit"};else if(op=="artistSongs")fields={"id","limit"};else if(op=="ranked")fields={"id","offset","limit","period"};else if(op=="artist" || op=="album" || op=="albumSongs")fields={"id"};
  if(fields.size()!=args.size())return false;for(auto it=args.begin();it!=args.end();++it){if(!fields.contains(it.key()))return false;const auto key=it.key();if(key=="page" || key=="limit" || key=="offset"){const auto n=number(it.value());if(it.value().type()==QVariant::String || n<0 || n>10000 || (key!="offset" && n<1) || (key=="page" && n>100))return false;}else{if(it.value().type()!=QVariant::String)return false;const auto s=it.value().toString();if(key=="id" && !QRegularExpression("^[0-9]{1,18}$").match(s).hasMatch())return false;if(key=="query" && (s.trimmed().isEmpty() || s.size()>256))return false;if(key=="period" && s.size()>128)return false;}}
  return (op!="search" || args.value("limit").toInt()<=50) && (op!="ranked" || args.value("offset").toInt()+args.value("limit").toInt()<=10000);
}
class Job:public std::enable_shared_from_this<Job>{
 public:
  NativePlatformHttp* http=nullptr;QUrl endpoint;QString op;QVariantMap args;QElapsedTimer elapsed;QVariantList songs,positions;QSet<QString> hashes,seen;int page=1;
  using Callback=std::function<void(QVariantMap)>;Callback done;
  QString id()const{return args.value("id").toString();}int limit()const{return args.value("limit",100).toInt();}
  void finish(QVariantMap data){data["backend"]="cpp-kugou-catalog";if(!data.contains("error")){if(json(data).toUtf8().size()>8*1024*1024){done(fail("response_too_large"));return;}qInfo("MCONNECT_NATIVE_KUGOU_CATALOG_OK operation=%s",qPrintable(op));}done(data);}
  void get(const QString& path,QList<QPair<QString,QString>> fields,Callback callback,bool beijing=false,bool home=false){
    const auto base=!endpoint.isEmpty()?endpoint:QUrl(home?"https://m.kugou.com":beijing?"http://mobilecdnbj.kugou.com":"http://mobilecdn.kugou.com");
    QNetworkRequest request(NativePlatformUrl(base.resolved(QUrl(path)),fields));request.setRawHeader("User-Agent","Mozilla/5.0 (Linux; Android 11) AppleWebKit/537.36");const int remaining=18000-int(elapsed.elapsed());if(remaining<=0){callback(fail("timeout"));return;}
    const auto self=shared_from_this();http->request(request,"GET",{},[self,callback](NativeHttpResult response){callback(response.error.isEmpty()?parse(response.body):fail(response.error));},remaining,8*1024*1024,true,true);
  }
  bool append(const QVariant& raw){if(raw.type()!=QVariant::List || raw.toList().size()>10000)return false;int added=0;for(const auto& value:raw.toList()){if(value.type()!=QVariant::Map)return false;const auto parsed=song(value.toMap());if(parsed.isEmpty())continue;songs.append(parsed);++added;}return raw.toList().isEmpty() || added>0;}
  void simple(const QString& path,QList<QPair<QString,QString>> fields,bool beijing=false){const auto self=shared_from_this();get(path,fields,[self](QVariantMap root){
    if(root.contains("error")){self->finish(root);return;}const auto data=root.value("data");
    if(self->op=="artist"){
      if(data.type()!=QVariant::Map){self->finish(fail("invalid_response"));return;}const auto raw=data.toMap();NativeArtist a;a.id=text(raw,{"singerid"});if(a.id.isEmpty())a.id=self->id();a.name=text(raw,{"singername"});if(a.name.isEmpty()){self->finish({{"artist",QVariant()}});return;}a.avatarUrl=image(text(raw,{"avatar","imgurl"}));a.briefDesc=text(raw,{"profile","intro"});for(const auto& field:QList<QPair<QString,QStringList>>{{"songCount",{"songcount"}},{"albumCount",{"albumcount"}},{"fansCount",{"fansnums","fanscount"}}}){const auto n=number(text(raw,field.second));if(n>=0){if(field.first=="songCount")a.songCount=n;else if(field.first=="albumCount")a.albumCount=n;else a.fansCount=n;}}self->finish({{"artist",a.json()}});return;
    }
    if(self->op=="album"){if(data.type()!=QVariant::Map){self->finish(fail("invalid_response"));return;}const auto parsed=album(data.toMap());self->finish({{"album",parsed.isEmpty()?QVariant():QVariant(parsed)}});return;}
    const auto raw=data.toMap().value("info");if(raw.type()!=QVariant::List){self->finish(fail("invalid_response"));return;}
    if(self->op=="artistAlbums"){QVariantList rows;for(const auto& value:raw.toList()){const auto parsed=album(value.toMap());if(!parsed.isEmpty())rows.append(parsed);}self->finish({{"albums",rows}});return;}
    if(!self->append(raw)){self->finish(fail("invalid_response"));return;}if(self->op=="search" || self->op=="artistSongs")self->songs=self->songs.mid(0,self->limit());self->finish({{"songs",self->songs}});
  },beijing);}
  void tracks(bool ranked=false){
    const auto self=shared_from_this();const int count=ranked?qMin(100,limit()):100;
    QList<QPair<QString,QString>> fields{{ranked?"rankid":"albumid",id()},{"page",QString::number(page)},{"pagesize",QString::number(count)},{"plat","0"},{"version","11309"}};
    get(ranked?"/api/v3/rank/song":"/api/v3/album/song",fields,[self,ranked,count](QVariantMap root){
      if(root.contains("error")){self->finish(root);return;}const auto data=root.value("data").toMap();const auto raw=data.value("info");if(raw.type()!=QVariant::List){self->finish(fail("invalid_response"));return;}const auto rows=raw.toList();
      if(!rows.isEmpty()){const auto hash=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument::fromVariant(rows).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());if(self->hashes.contains(hash)){self->finish(fail("pagination_stalled"));return;}self->hashes.insert(hash);}
      for(int i=0;i<rows.size();++i){if(ranked && (self->page-1)*count+i<self->args.value("offset").toInt())continue;const auto row=rows[i].toMap();const auto parsed=song(row);if(parsed.isEmpty())continue;self->songs.append(parsed);
        if(ranked){const auto current=number(row.value("sort"),(self->page-1)*count+i+1),previous=number(row.value("rank_count"));QVariantMap position{{"rank",current}};if(previous>0)position["rankChange"]=previous-current;if(number(row.value("isfirst"))==1)position["isNew"]=true;const auto hint=text(row,{"rank_count"});if(!hint.isEmpty())position["rankValue"]=hint;self->positions.append(position);}
        if(ranked && self->songs.size()>=self->limit())break;
      }
      const auto total=number(data.value("total",data.value("songcount")));const int read=self->page*count;
      if((ranked && self->songs.size()>=self->limit()) || rows.size()<count || (total>=0 && read>=total)){
        if(!ranked && total>self->songs.size()){self->finish(fail("incomplete_response"));return;}
        QVariantMap result{{"songs",self->songs}};if(ranked)result["positions"]=self->positions;self->finish(result);return;
      }
      if(read>=10000 || self->songs.size()>10000){self->finish(fail("response_too_large"));return;}++self->page;self->tracks(ranked);
    });
  }
  void toplists(){const auto self=shared_from_this();get("/api/v3/rank/list",{{"format","json"},{"withsong","0"},{"plat","0"},{"page","1"},{"pagesize","100"}},[self](QVariantMap root){
    if(root.contains("error")){self->finish(root);return;}const auto raw=root.value("data").toMap().value("info");if(raw.type()!=QVariant::List || raw.toList().size()>1000){self->finish(fail("invalid_response"));return;}QVariantList charts;
    for(const auto& value:raw.toList()){const auto row=value.toMap();const auto id=text(row,{"rankid"}),name=text(row,{"rankname"});if(id.isEmpty() || name.isEmpty())continue;QVariantMap chart{{"id",id},{"name",name},{"coverUrl",image(text(row,{"imgurl"}))}};const auto extra=row.value("extra").toMap().value("resp").toMap();auto n=number(row.value("songcount"));if(n<0)n=number(text(extra,{"all_total","total"}));if(n>=0)chart["songCount"]=n;for(const auto& p:QList<QPair<QString,QString>>{{"updateFrequency","update_frequency"},{"period","rank_id_publish_date"},{"intro","intro"}}){const auto s=text(row,{p.second});if(!s.isEmpty())chart[p.first]=s;}charts.append(chart);}self->finish({{"charts",charts}});
  });}
  void homepage(){const auto self=shared_from_this();get("/",{{"json","true"}},[self](QVariantMap root){
    if(root.contains("error")){self->finish(root);return;}auto raw=root.value("data");if(raw.type()!=QVariant::List){self->finish(fail("invalid_response"));return;}auto rows=raw.toList();for(const auto& item:root.value("special").toMap().value("list").toMap().value("info").toList())for(const auto& s:item.toMap().value("songs").toList())rows.append(s);
    if(rows.size()>10000){self->finish(fail("response_too_large"));return;}for(const auto& value:rows){const auto parsed=song(value.toMap());const auto id=parsed.value("id").toString();if(id.isEmpty() || self->seen.contains(id))continue;self->seen.insert(id);self->songs.append(parsed);if(self->songs.size()>=30)break;}self->finish({{"songs",self->songs}});
  },false,true);}
  void start(){
    if(op=="search"){simple("/api/v3/search/song",{{"format","json"},{"keyword",args.value("query").toString()},{"page",QString::number(args.value("page").toInt())},{"pagesize",QString::number(limit())},{"showtype","1"}});return;}
    if(op=="artist"){simple("/api/v3/singer/info",{{"singerid",id()}},true);return;}
    if(op=="artistSongs"){simple("/api/v3/singer/song",{{"singerid",id()},{"page","1"},{"pagesize",QString::number(limit())},{"sorttype","2"},{"version","9108"},{"identity","3"},{"plat","0"},{"area_code","1"}},true);return;}
    if(op=="artistAlbums"){simple("/api/v3/singer/album",{{"singerid",id()},{"page",QString::number(args.value("page").toInt())},{"pagesize",QString::number(limit())},{"plat","0"}});return;}
    if(op=="album"){simple("/api/v3/album/info",{{"albumid",id()},{"format","json"}});return;}
    if(op=="albumSongs"){tracks();return;}if(op=="ranked"){page=args.value("offset").toInt()/qMin(100,limit())+1;tracks(true);return;}if(op=="toplists"){toplists();return;}if(op=="homepage"){homepage();return;}simple("/api/v3/recommend/song",{{"format","json"}});
  }
};
}
QVariantMap ParseNativeKugouSong(const QVariantMap& data) { return song(data); }
QString NativePlatforms::KugouCatalog(const QString& op,const QString& arguments){
  QJsonParseError error;const auto doc=QJsonDocument::fromJson(arguments.toUtf8(),&error);if(!operations.contains(op) || arguments.toUtf8().size()>16384 || error.error!=QJsonParseError::NoError || !doc.isObject() || !valid(op,doc.toVariant().toMap()))return json(fail("invalid_arguments"));if(!calledFromDBus())return json(fail("dbus_required"));
  setDelayedReply(true);const auto bus=connection();const auto call=message();auto job=std::make_shared<Job>();job->http=&http_;job->endpoint=kugouEndpoint_;job->op=op;job->args=doc.toVariant().toMap();job->done=[bus,call](QVariantMap data){bus.send(call.createReply(QVariantList{json(data)}));};job->elapsed.start();job->start();return {};
}
