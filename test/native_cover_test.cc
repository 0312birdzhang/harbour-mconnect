// Run under dbus-run-session: no real player or audio device is contacted.
#include "sailfish_cover.h"
#include "cover_artwork.h"
#include <QCoreApplication>
#include <QDBusVirtualObject>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QTemporaryDir>
#include <QThread>
#include <QDebug>
#include <functional>
#include <cassert>
namespace {
const char* service="org.mpris.MediaPlayer2.harbour_mconnect";
const char* path="/org/mpris/MediaPlayer2";
const char* iface="org.mpris.MediaPlayer2.Player";
bool Wait(const std::function<bool()>& condition){for(int i=0;i<300;++i){QCoreApplication::processEvents();if(condition())return true;QThread::msleep(10);}return false;}
class FakePlayer:public QDBusVirtualObject {
public:
 QVariantMap metadata,properties;
 QStringList calls;
 QString introspect(const QString&)const override{return "";}
 bool handleMessage(const QDBusMessage& message,const QDBusConnection& bus)override{
  if(message.member()=="GetAll"){
   auto all=properties;all["Metadata"]=metadata;bus.send(message.createReply(QVariantList{all}));return true;
  }
  if(message.member()=="PlayPause" || message.member()=="Next"){
   calls<<message.member();
   if(message.member()=="PlayPause")properties["PlaybackStatus"]=properties["PlaybackStatus"]=="Playing"?"Paused":"Playing";
   else metadata["xesam:title"]="next-title";
   bus.send(message.createReply());Notify();return true;
  }
  return false;
 }
 void Notify(){auto signal=QDBusMessage::createSignal(path,"org.freedesktop.DBus.Properties","PropertiesChanged");signal<<QString(iface)<<properties<<QStringList();QDBusConnection::sessionBus().send(signal);}
};
}
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);QTemporaryDir directory;assert(directory.isValid());
 QImage image(80,80,QImage::Format_RGB32);image.fill(Qt::white);
 assert(CoverTitleInk(image,image.rect())==QColor("#111111"));
 image.fill(Qt::black);assert(CoverTitleInk(image,image.rect())==QColor(Qt::white));
 for(int y=0;y<40;++y)for(int x=0;x<80;++x)image.setPixelColor(x,y,Qt::white);
 assert(CoverTitleInk(image,QRect(0,0,80,40))==QColor("#111111"));
 assert(CoverTitleInk(image,QRect(0,40,80,40))==QColor(Qt::white));
 qInfo("PASS title ink follows the actual background region");
 const QString file=directory.path()+"/art.png";assert(image.save(file));
 FakePlayer fake;fake.metadata={{"xesam:title",QString::fromUtf8("\xe6\xb5\x8b\xe8\xaf\x95\xe6\xad\x8c\xe6\x9b\xb2")},{"mpris:artUrl","file://"+file}};
 fake.properties={{"PlaybackStatus","Paused"},{"CanPlay",true},{"CanGoNext",true}};
 auto bus=QDBusConnection::sessionBus();assert(bus.registerService(service));assert(bus.registerVirtualObject(path,&fake));
 SailfishCoverModel model;
 assert(Wait([&](){return !model.playback().value("title").toString().isEmpty();}));
 assert(!model.artwork().isNull());assert(!model.playback().value("playing").toBool());
 qInfo("PASS paused metadata and local artwork");
 auto trigger=[&](int id){auto call=QDBusMessage::createMethodCall(model.actionService(),"/"+QString(model.actionService()).replace('.','/'),"org.sailfishos.silica.coveraction","trigger");call<<id;bus.asyncCall(call);};
 trigger(0);assert(Wait([&](){return fake.calls.contains("PlayPause") && model.playback().value("playing").toBool();}));
 trigger(0);assert(Wait([&](){return fake.calls.count("PlayPause")==2 && !model.playback().value("playing").toBool();}));
 qInfo("PASS native D-Bus play/pause action and icon state");
 trigger(1);assert(Wait([&](){return model.playback().value("title")=="next-title";}));assert(fake.calls.last()=="Next");
 qInfo("PASS native D-Bus next action refreshes the song");
 fake.metadata.clear();fake.properties["CanPlay"]=false;fake.Notify();
 assert(Wait([&](){return model.playback().value("title").toString().isEmpty();}));
 const int count=fake.calls.size();trigger(0);trigger(1);trigger(99);
 for(int i=0;i<30;++i){QCoreApplication::processEvents();QThread::msleep(10);}assert(fake.calls.size()==count);
 bus.unregisterService(service);model.refresh();assert(Wait([&](){return model.playback().isEmpty();}));
 qInfo("PASS empty/unavailable player does not accept playback actions");
 return 0;
}
