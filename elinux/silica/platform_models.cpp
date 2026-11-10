#include "platform_models.h"
#include <QUrl>
#include <QStringList>
#include <QDateTime>
#include <cmath>

namespace {
qint64 integer(const QVariant& value,qint64 fallback=0) {
  bool ok=false;const auto result=value.toLongLong(&ok);
  if(!ok)return fallback;
  if(value.type()==QVariant::Double && (!std::isfinite(value.toDouble()) || value.toDouble()!=double(result)))return fallback;
  return result;
}
QString id(const QVariant& value) {
  if(value.type()==QVariant::String)return value.toString();
  if(value.type()==QVariant::Bool || value.type()==QVariant::Invalid)return {};
  // QJson stores numbers as doubles: converting directly to QString can round
  // large song IDs. Integer formatting is mandatory for stable download keys.
  if(value.type()==QVariant::Double && std::abs(value.toDouble())>9007199254740991.0)return {};
  const auto number=integer(value,-1);return number<0?QString():QString::number(number);
}
QString image(const QVariant& value) {
  QString text=value.toString();if(text.startsWith("//"))text="https:"+text;
  const QUrl url(text);return (url.scheme()=="https" || url.scheme()=="http")?text:QString();
}
QVariant optionalInteger(const QVariantMap& value,const QString& name) {
  if(!value.contains(name) || value.value(name).isNull())return {};
  bool ok=false;const auto number=value.value(name).toLongLong(&ok);
  if(value.value(name).type()==QVariant::Double && value.value(name).toDouble()!=double(number))return {};
  return ok?QVariant(number):QVariant();
}
}
QVariantMap NativeArtist::json() const {
  QVariantMap result{{"id",id},{"name",name}};
  if(!avatarUrl.isEmpty())result["avatarUrl"]=avatarUrl;
  if(!briefDesc.isEmpty())result["briefDesc"]=briefDesc;
  if(songCount.isValid() && songCount.toLongLong()>=0)result["songCount"]=songCount;
  if(albumCount.isValid() && albumCount.toLongLong()>=0)result["albumCount"]=albumCount;
  if(fansCount.isValid() && fansCount.toLongLong()>=0)result["fansCount"]=fansCount;
  return result;
}
QVariantMap NativeAlbum::json() const {
  QVariantMap result{{"id",id},{"name",name},{"coverUrl",coverUrl}};
  for(const auto& field:QList<QPair<QString,QString>>{{"artistId",artistId},{"artistName",artistName},{"releaseDate",releaseDate},{"description",description},{"company",company},{"genre",genre}})if(!field.second.isEmpty())result[field.first]=field.second;
  if(songCount.isValid() && songCount.toLongLong()>=0)result["songCount"]=songCount;
  return result;
}
QVariantMap NativeSong::json() const {
  QVariantList entries;for(const auto& artist:artists)entries.append(artist.json());
  QVariantMap result{{"id",id},{"platform",platform},{"name",name},{"coverUrl",coverUrl},{"durationMs",durationMs},{"artists",entries}};
  if(hasAlbum)result["album"]=album.json();
  if(!album.id.isEmpty())result["albumId"]=album.id;
  if(!artists.isEmpty() && !artists.first().id.isEmpty())result["artistId"]=artists.first().id;
  if(trackNumber.isValid())result["trackNumber"]=trackNumber;
  if(fee.isValid())result["fee"]=fee;
  return result;
}
QVariantMap NativePlaylist::json() const {
  return {{"id",id},{"platform",platform},{"name",name},{"coverUrl",coverUrl},{"songCount",songCount},{"creatorName",creatorName},{"editable",false},{"collected",true}};
}
NativeSong ParseNeteaseSong(const QVariantMap& value) {
  NativeSong song;song.id=id(value.value("id"));song.platform="netease";song.name=value.value("name").toString();
  const auto rawArtists=value.value(value.value("ar").type()==QVariant::List?"ar":"artists").toList();
  for(const auto& item:rawArtists){if(item.type()!=QVariant::Map)continue;const auto raw=item.toMap();song.artists.append({id(raw.value("id")),raw.value("name").toString()});}
  const auto album=value.value(value.value("al").type()==QVariant::Map?"al":"album");
  if(album.type()==QVariant::Map){const auto raw=album.toMap();song.album={id(raw.value("id")),raw.value("name").toString(),image(raw.value("picUrl"))};song.hasAlbum=true;song.coverUrl=song.album.coverUrl;}
  song.durationMs=integer(value.value("dt"),-1);if(song.durationMs<0)song.durationMs=integer(value.value("duration"));
  if(song.durationMs<0 || song.durationMs>7LL*24*60*60*1000)song.durationMs=0;
  song.trackNumber=optionalInteger(value,"no");song.fee=optionalInteger(value,"fee");return song;
}
NativePlaylist ParseNeteasePlaylist(const QVariantMap& value) {
  NativePlaylist list;list.id=id(value.value("id"));list.platform="netease";list.name=value.value("name").toString();
  list.songCount=integer(value.value("trackCount"),-1);if(list.songCount<0)list.songCount=integer(value.value("bookCount"));
  if(list.songCount<0)list.songCount=0;
  for(const auto& field:QStringList{"coverImgUrl","coverUrl","picUrl","imgurl"}){list.coverUrl=image(value.value(field));if(!list.coverUrl.isEmpty())break;}
  list.creatorName=value.value("creator").toMap().value("nickname").toString();return list;
}
NativeArtist ParseNeteaseArtist(const QVariantMap& value) {
  NativeArtist artist;artist.id=id(value.value("id"));artist.name=value.value("name").toString();
  for(const auto& field:QStringList{"cover","picUrl","avatar"}){artist.avatarUrl=image(value.value(field));if(!artist.avatarUrl.isEmpty())break;}
  artist.briefDesc=value.value("briefDesc").toString();artist.songCount=optionalInteger(value,"musicSize");
  artist.albumCount=optionalInteger(value,"albumSize");artist.fansCount=optionalInteger(value,"fansCount");return artist;
}
NativeAlbum ParseNeteaseAlbum(const QVariantMap& value) {
  NativeAlbum album;album.id=id(value.value("id"));album.name=value.value("name").toString();album.coverUrl=image(value.value("picUrl"));
  auto artist=value.value("artist").toMap();
  if(artist.isEmpty() && !value.value("artists").toList().isEmpty())artist=value.value("artists").toList().first().toMap();
  album.artistId=id(artist.value("id"));album.artistName=artist.value("name").toString();
  const auto milliseconds=integer(value.value("publishTime"));
  // Match Dart's previous local DateTime, including a date near UTC midnight.
  // Omit the offset so Dart retains local calendar time rather than converting
  // an explicit +08:00 suffix back to UTC before formatting the album date.
  if(milliseconds>0)album.releaseDate=QDateTime::fromMSecsSinceEpoch(milliseconds).toString("yyyy-MM-dd'T'HH:mm:ss.zzz");
  album.description=value.value("description").toString();album.company=value.value("company").toString();album.songCount=optionalInteger(value,"size");
  QStringList tags;for(const auto& tag:value.value("tags").toList()){const auto text=tag.toString().trimmed();if(!text.isEmpty())tags.append(text);}album.genre=tags.join('/');return album;
}
