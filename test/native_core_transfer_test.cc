// Run with dbus-run-session: never use the user's real music bus.
#include <QCoreApplication>
#include <QDBusConnection>
#include <QJsonDocument>
#include <QTimer>
#include <cstdio>
#include "../elinux/silica/bridge.h"

class MockCore : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.sailfishos.Mconnect.Core")
 public:
  QString text = QString(120000,QChar(0x4e2d)) + QString::fromUtf8("中文🎵");
  QByteArray bytes = QJsonDocument::fromVariant(QVariantMap{{"text",text}}).toJson(QJsonDocument::Compact);
 public slots:
  QString Request(const QString& method,const QString& args) {
    const auto input=QJsonDocument::fromJson(args.toUtf8()).toVariant().toMap();
    QVariantMap result;
    if(method=="large")result={{"_mconnectTransfer","test"},{"parts",(bytes.size()+24575)/24576}};
    else if(method=="_readTransfer")result={{"data",QString::fromLatin1(bytes.mid(input.value("index").toInt()*24576,24576).toBase64())}};
    return QString::fromUtf8(QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact));
  }
};
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  auto bus=QDBusConnection::sessionBus();
  MockCore mock;
  if(!bus.registerService("org.sailfishos.Mconnect.Core") || !bus.registerObject(
      "/org/sailfishos/Mconnect/Core",&mock,QDBusConnection::ExportAllSlots))return 1;
  CoreBridge bridge(nullptr,false);
  const int id=bridge.request("large");
  QObject::connect(&bridge,&CoreBridge::completed,&app,[&](int request,const QVariant& value,const QString& error){
    if(request!=id)return;
    const bool passed=error.isEmpty() && value.toMap().value("text").toString()==mock.text;
    fprintf(stderr,"NATIVE_CORE_TRANSFER_%s bytes=%d\n",passed?"PASS":"FAIL",mock.bytes.size());
    app.exit(passed?0:1);
  });
  QTimer::singleShot(10000,&app,[&](){app.exit(2);});
  return app.exec();
}
#include "native_core_transfer_test.moc"
