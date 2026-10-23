#pragma once
#include <QObject>
#include <QVariantMap>
#include <QImage>
class QNetworkAccessManager;
class SailfishCoverModel : public QObject {
 Q_OBJECT
 Q_CLASSINFO("D-Bus Interface", "org.sailfishos.silica.coveraction")
public:
 explicit SailfishCoverModel(QObject* parent=nullptr);
 QVariantMap playback() const { return state_; }
 QImage artwork() const { return artwork_; }
 QString actionService() const { return actionService_; }
 void command(const QString& method);
public slots:
 void refresh();
 void propertiesChanged(const QString&, const QVariantMap&, const QStringList&);
 void trigger(int id);
signals:
 void changed();
private:
 void loadArtwork(const QString& url);
 QVariantMap state_;
 QImage artwork_;
 QString artUrl_, actionService_;
 QNetworkAccessManager* network_;
 bool pending_=false;
};
void StartSailfishCover(const char* bundle);
void PollSailfishCover();
void RaiseSailfishPhone();
bool SailfishWindowCloseRequested();
void StopSailfishCover();
