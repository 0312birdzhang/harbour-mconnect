// The cover is an independent wl_shm surface. Qt paints images only, using
// a separate Qt connection without any Qt windows. Flutter owns its input.
#include "sailfish_cover.h"
#include "cover_artwork.h"
#include <QGuiApplication>
#include <QTimer>
#include <QDataStream>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusArgument>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QDir>
#include <QPainter>
#include <QTextLayout>
#include <QFontMetrics>
#include <QDebug>
#include <QtWaylandClient/private/qwayland-surface-extension.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <memory>
#include <cstring>
namespace {
wl_display* display=nullptr;
wl_surface* phoneSurface=nullptr;
wl_surface* coverSurface=nullptr;
wl_shell_surface* coverShell=nullptr;
wl_compositor* compositor=nullptr;
wl_shell* shell=nullptr;
wl_shm* shm=nullptr;
QGuiApplication* application=nullptr;
SailfishCoverModel* model=nullptr;
bool dirty=true, exposed=false;
bool windowCloseRequested=false;
int width=420,height=560;
constexpr uint64_t coverId=1296253778;
std::unique_ptr<QtWayland::qt_surface_extension> extension;
std::unique_ptr<QtWayland::qt_extended_surface> phone,cover;
const char* service="org.mpris.MediaPlayer2.harbour_mconnect";
const char* path="/org/mpris/MediaPlayer2";
void Property(QtWayland::qt_extended_surface* surface,const QString& key,const QVariant& value) {
 QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
 stream.setVersion(QDataStream::Qt_5_0);stream<<value;
 surface->update_generic_property(key,bytes);
}
void Global(void*,wl_registry* registry,uint32_t id,const char* name,uint32_t) {
 if(!strcmp(name,"qt_surface_extension")) extension.reset(new QtWayland::qt_surface_extension(registry,id,1));
 else if(!strcmp(name,"wl_compositor")) compositor=static_cast<wl_compositor*>(wl_registry_bind(registry,id,&wl_compositor_interface,1));
 else if(!strcmp(name,"wl_shell")) shell=static_cast<wl_shell*>(wl_registry_bind(registry,id,&wl_shell_interface,1));
 else if(!strcmp(name,"wl_shm")) shm=static_cast<wl_shm*>(wl_registry_bind(registry,id,&wl_shm_interface,1));
}
void Removed(void*,wl_registry*,uint32_t) {}
const wl_registry_listener registryListener={Global,Removed};
class PhoneProperties : public QtWayland::qt_extended_surface {
public: explicit PhoneProperties(::qt_extended_surface* p):QtWayland::qt_extended_surface(p){}
protected:
 void extended_surface_close() override {
  fprintf(stderr,"Mconnect phone close requested\n");
  windowCloseRequested=true;
 }
};
class CoverProperties : public QtWayland::qt_extended_surface {
public: explicit CoverProperties(::qt_extended_surface* p):QtWayland::qt_extended_surface(p){}
protected:
 void extended_surface_close() override {
  fprintf(stderr,"Mconnect cover close requested\n");
  windowCloseRequested=true;
 }
 void extended_surface_onscreen_visibility(int32_t visible) override {
  exposed=visible!=0;dirty=true;if(exposed && model) model->refresh();
 }
};
struct Buffer {void* data;size_t size;wl_buffer* buffer;};
void Released(void* data,wl_buffer*) {
 auto* b=static_cast<Buffer*>(data);wl_buffer_destroy(b->buffer);munmap(b->data,b->size);delete b;
}
const wl_buffer_listener bufferListener={Released};
void Paint() {
 if(!coverSurface || !model || !shm) return;
 QByteArray pattern=qgetenv("XDG_RUNTIME_DIR")+"/mconnect-cover-XXXXXX";
 int fd=mkstemp(pattern.data());if(fd<0)return;unlink(pattern.constData());
 const size_t bytes=static_cast<size_t>(width)*height*4;
 if(ftruncate(fd,bytes)!=0){close(fd);return;}
 void* pixels=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
 if(pixels==MAP_FAILED){close(fd);return;}
 auto* pool=wl_shm_create_pool(shm,fd,bytes);close(fd);
 auto* b=new Buffer{pixels,bytes,wl_shm_pool_create_buffer(pool,0,width,height,width*4,WL_SHM_FORMAT_ARGB8888)};
 wl_shm_pool_destroy(pool);wl_buffer_add_listener(b->buffer,&bufferListener,b);
 QImage image(static_cast<uchar*>(pixels),width,height,width*4,QImage::Format_ARGB32_Premultiplied);
 image.fill(QColor("#17222d"));
 {
  QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);
  const int margin=qMax(8,width/24),controlsHeight=width/5;
  QFont font("Sail Sans Pro Light");font.setPixelSize(qMax(16,width/15));painter.setFont(font);
  const int titleHeight=QFontMetrics(font).height()*2;
  QRect titleArea(margin,height-controlsHeight-titleHeight-margin,width-2*margin,titleHeight);
  const auto artwork=model->artwork();
  if(!artwork.isNull()) {
   const auto scaled=artwork.scaled(image.size(),Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);
   painter.drawImage(image.rect(),scaled,QRect((scaled.width()-width)/2,(scaled.height()-height)/2,width,height));
  } else {
   QFont icon("Sail Sans Pro Light");icon.setPixelSize(width/4);painter.setFont(icon);painter.setPen(QColor("#92afca"));
   painter.drawText(QRect(0,0,width,titleArea.top()),Qt::AlignCenter,QString::fromUtf8("\xe2\x99\xaa"));painter.setFont(font);
  }
  const QColor ink=CoverTitleInk(image,titleArea);
  const bool darkInk=ink!=QColor(Qt::white);
  QLinearGradient shade(0,titleArea.top()-margin,0,titleArea.bottom()+margin);
  const QColor base=darkInk?QColor(255,255,255):QColor(0,0,0);
  shade.setColorAt(0,QColor(base.red(),base.green(),base.blue(),0));
  shade.setColorAt(.3,QColor(base.red(),base.green(),base.blue(),160));
  shade.setColorAt(1,QColor(base.red(),base.green(),base.blue(),200));
  painter.fillRect(QRect(0,titleArea.top()-margin,width,titleHeight+2*margin),shade);
  // Native cover buttons use the system's light icons; keep their strip readable.
  painter.fillRect(QRect(0,height-controlsHeight,width,controlsHeight),QColor(0,0,0,150));
  painter.setPen(ink);
  QString title=model->playback().value("title").toString();if(title.isEmpty())title="Mconnect";
  QTextLayout text(title,font);QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);text.setTextOption(option);
  text.beginLayout();qreal y=titleArea.top();
  for(int i=0;i<2;++i){auto line=text.createLine();if(!line.isValid())break;line.setLineWidth(titleArea.width());
   if(i==1){const auto tail=title.mid(line.textStart());painter.drawText(QPointF(margin,y+QFontMetrics(font).ascent()),QFontMetrics(font).elidedText(tail,Qt::ElideRight,titleArea.width()));break;}
   line.draw(&painter,QPointF(margin,y));y+=line.height();
  }
  text.endLayout();
 }
 wl_surface_attach(coverSurface,b->buffer,0,0);wl_surface_damage(coverSurface,0,0,width,height);wl_surface_commit(coverSurface);
}
void Ping(void*,wl_shell_surface* surface,uint32_t serial){wl_shell_surface_pong(surface,serial);}
void Configure(void*,wl_shell_surface*,uint32_t,int32_t w,int32_t h){
 if(w>0 && h>0 && w<=1600 && h<=1600){width=w;height=h;dirty=true;}
}
void PopupDone(void*,wl_shell_surface*){}
const wl_shell_surface_listener shellListener={Ping,Configure,PopupDone};
void UpdateActions() {
 const auto state=model->playback();QString value;
 if(state.value("canPlay").toBool() && !state.value("title").toString().isEmpty()) {
  const QString endpoint=model->actionService()+"\ntrigger\n";
  value=endpoint+"0\nimage://theme/icon-cover-"+(state.value("playing").toBool()?"pause":"play");
  if(state.value("canNext").toBool())value+="\n"+endpoint+"1\nimage://theme/icon-cover-next-song";
  value+="\n0";
 }
 Property(phone.get(),"_APP_COVER_ACTION",value);
}
}
// Observe the existing public Wayland handles, without sharing Qt's input code.
extern "C" __attribute__((visibility("default"))) wl_display* wl_display_connect(const char* name) {
 auto real=reinterpret_cast<wl_display*(*)(const char*)>(dlsym(RTLD_NEXT,"wl_display_connect"));
 auto* result=real(name);if(!display)display=result;return result;
}
extern "C" __attribute__((visibility("default"))) wl_egl_window* wl_egl_window_create(wl_surface* surface,int w,int h) {
 if(!phoneSurface)phoneSurface=surface;
 auto real=reinterpret_cast<wl_egl_window*(*)(wl_surface*,int,int)>(dlsym(RTLD_NEXT,"wl_egl_window_create"));return real(surface,w,h);
}
SailfishCoverModel::SailfishCoverModel(QObject* parent):QObject(parent),network_(new QNetworkAccessManager(this)) {
 actionService_=QString("org.sailfishos.coveraction.harbour_mconnect.pid%1.id1").arg(getpid());
 QDBusConnection::sessionBus().registerService(actionService_);
 QDBusConnection::sessionBus().registerObject("/"+QString(actionService_).replace('.','/'),this,QDBusConnection::ExportAllSlots);
 QDBusConnection::sessionBus().connect(service,path,"org.freedesktop.DBus.Properties","PropertiesChanged",this,SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
 auto* timer=new QTimer(this);timer->setInterval(5000);
 connect(timer,&QTimer::timeout,this,[this](){if(exposed)refresh();});timer->start();refresh();
}
void SailfishCoverModel::propertiesChanged(const QString& iface,const QVariantMap&,const QStringList&){if(iface=="org.mpris.MediaPlayer2.Player")refresh();}
void SailfishCoverModel::refresh() {
 if(pending_)return;pending_=true;
 auto call=QDBusMessage::createMethodCall(service,path,"org.freedesktop.DBus.Properties","GetAll");call<<QString("org.mpris.MediaPlayer2.Player");
 auto* watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call,1500),this);
 connect(watcher,&QDBusPendingCallWatcher::finished,this,[this](QDBusPendingCallWatcher* w){
  QDBusPendingReply<QVariantMap> reply=*w;pending_=false;QVariantMap next;
  if(!reply.isError()){
   const auto properties=reply.value();const auto metadata=qdbus_cast<QVariantMap>(properties.value("Metadata"));
   next["title"]=metadata.value("xesam:title");next["art"]=metadata.value("mpris:artUrl");
   next["playing"]=properties.value("PlaybackStatus")=="Playing";next["canPlay"]=properties.value("CanPlay");next["canNext"]=properties.value("CanGoNext");
  }
  if(next!=state_){state_=next;loadArtwork(next.value("art").toString());emit changed();}w->deleteLater();
 });
}
void SailfishCoverModel::loadArtwork(const QString& url) {
 if(url==artUrl_)return;artUrl_=url;artwork_=QImage();
 QUrl address(url);if(address.isLocalFile()){artwork_.load(address.toLocalFile());return;}
 if(address.scheme()!="http" && address.scheme()!="https")return;
 QNetworkRequest request(address);request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,QNetworkRequest::PreferCache);
 auto* reply=network_->get(request);reply->setReadBufferSize(8*1024*1024);
 QTimer::singleShot(8000,reply,&QNetworkReply::abort);
 connect(reply,&QNetworkReply::finished,this,[this,reply,url](){
  if(url==artUrl_ && reply->error()==QNetworkReply::NoError && reply->bytesAvailable()<=8*1024*1024){artwork_.loadFromData(reply->readAll());emit changed();}reply->deleteLater();
 });
}
void SailfishCoverModel::command(const QString& method) {
 if(method!="PlayPause" && method!="Next")return;
 const auto call=QDBusMessage::createMethodCall(service,path,"org.mpris.MediaPlayer2.Player",method);QDBusConnection::sessionBus().asyncCall(call,1500);
}
void SailfishCoverModel::trigger(int id) {
 if(!state_.value("canPlay").toBool() || state_.value("title").toString().isEmpty())return;
 if(id==0)command("PlayPause");else if(id==1 && state_.value("canNext").toBool())command("Next");
}
void StartSailfishCover(const char*) {
 if(!display || !phoneSurface)return;
 wl_registry* registry=wl_display_get_registry(display);wl_registry_add_listener(registry,&registryListener,nullptr);wl_display_roundtrip(display);
 if(!extension || !compositor || !shell || !shm){wl_registry_destroy(registry);return;}
 windowCloseRequested=false;
 phone.reset(new PhoneProperties(extension->get_extended_surface(phoneSurface)));
 static int argc=3;static char name[]="harbour-mconnect",platform[]="-platform",minimal[]="wayland";static char* argv[]={name,platform,minimal,nullptr};
 application=new QGuiApplication(argc,argv);application->setQuitOnLastWindowClosed(false);
 model=new SailfishCoverModel(application);
 QObject::connect(model,&SailfishCoverModel::changed,[]{dirty=true;UpdateActions();});
 coverSurface=wl_compositor_create_surface(compositor);cover.reset(new CoverProperties(extension->get_extended_surface(coverSurface)));
 Property(cover.get(),"WINID",QVariant::fromValue<qulonglong>(coverId));Property(cover.get(),"CATEGORY",QString("cover"));Property(cover.get(),"TRANSPARENT",false);
 coverShell=wl_shell_get_shell_surface(shell,coverSurface);wl_shell_surface_add_listener(coverShell,&shellListener,nullptr);wl_shell_surface_set_class(coverShell,"harbour-mconnect");wl_shell_surface_set_title(coverShell,"Mconnect cover");wl_shell_surface_set_toplevel(coverShell);
 Property(phone.get(),"SAILFISH_COVER_WINDOW",QString("__winref:%1").arg(coverId));Property(phone.get(),"SAILFISH_HAVE_COVER",true);
 UpdateActions();Paint();wl_display_flush(display);wl_registry_destroy(registry);qInfo("Mconnect native cover ready (shm, isolated input)");
}
void RaiseSailfishPhone(){
 if(!phone || !display)return;
 phone->raise();
 wl_display_flush(display);
}
bool SailfishWindowCloseRequested(){return windowCloseRequested;}
void PollSailfishCover(){if(!application)return;application->processEvents();if(dirty && exposed){dirty=false;Paint();}wl_display_flush(display);}
void StopSailfishCover(){
 if(!application)return;
 Property(phone.get(),"_APP_COVER_ACTION",QString());
 if(coverShell)wl_shell_surface_destroy(coverShell);
 if(coverSurface)wl_surface_destroy(coverSurface);
 cover.reset();phone.reset();extension.reset();
 delete application;application=nullptr;model=nullptr;
}
