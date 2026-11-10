// Run under dbus-run-session: this never contacts the user's playback service.
#include "../elinux/silica/bridge.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QJsonDocument>
#include <QTimer>
#include <sqlite3.h>
#include <cstdio>

class LyricsMockCore : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.sailfishos.Mconnect.Core")
 public:
  int lyricCalls=0;
 public slots:
  QString Request(const QString& method,const QString&) {
    QVariantMap result;
    if(method=="snapshot")result={{"song",QVariantMap{{"id","test"},{"platform","netease"}}}};
    else if(method=="lyrics"){++lyricCalls;result={{"loading",false},{"error",false},{"backend","fallback"}};}
    return QString::fromUtf8(QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact));
  }
};
int main(int argc,char** argv) {
  QTemporaryDir home;
  if(!home.isValid())return 1;
  qputenv("HOME",home.path().toUtf8());
  qputenv("XDG_DATA_HOME",(home.path()+"/.local/share").toUtf8());
  QCoreApplication app(argc,argv);
  const auto directory=home.path()+"/.local/share/harbour-mconnect";
  QDir().mkpath(directory);
  sqlite3* db=nullptr;
  if(sqlite3_open((directory+"/mconnect.sqlite").toUtf8().constData(),&db)!=SQLITE_OK)return 1;
  if(sqlite3_exec(db,"CREATE TABLE lyrics_cache(song_id TEXT,platform TEXT,content TEXT,format TEXT);"
      "INSERT INTO lyrics_cache VALUES('test','netease','[00:01.00]native','lrc');",nullptr,nullptr,nullptr)!=SQLITE_OK)return 1;
  auto bus=QDBusConnection::sessionBus();
  LyricsMockCore mock;
  if(!bus.registerService("org.sailfishos.Mconnect.Core") || !bus.registerObject(
      "/org/sailfishos/Mconnect/Core",&mock,QDBusConnection::ExportAllSlots))return 1;
  CoreBridge bridge;
  int fallbackId=0;
  bool sawNative=false;
  QObject::connect(&bridge,&CoreBridge::completed,&app,[&](int id,const QVariant& value,const QString& error){
    const auto backend=value.toMap().value("backend").toString();
    if(!sawNative && backend=="cpp-lrc-cache") {
      if(!error.isEmpty() || mock.lyricCalls!=0){app.exit(1);return;}
      sawNative=true;
      if(sqlite3_exec(db,"UPDATE lyrics_cache SET format='qrc'",nullptr,nullptr,nullptr)!=SQLITE_OK){app.exit(1);return;}
      // Initial connection may queue two reads. Wait for those to drain.
      QTimer::singleShot(200,&app,[&](){fallbackId=bridge.request("lyrics");});
    } else if(fallbackId && id==fallbackId) {
      const bool ok=error.isEmpty() && backend=="fallback" && mock.lyricCalls>0;
      fprintf(stderr,"NATIVE_LYRICS_BRIDGE_%s local-first asynchronous unsupported-format-fallback\n",ok?"PASS":"FAIL");
      app.exit(ok?0:1);
    }
  });
  QTimer::singleShot(5000,&app,[&](){app.exit(2);});
  const int result=app.exec();
  sqlite3_close(db);
  return result;
}
#include "native_lyrics_bridge_test.moc"
