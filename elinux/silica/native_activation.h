#pragma once

#include <QObject>

class NativeActivation : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.sailfishos.Mconnect")
 public:
  explicit NativeActivation(QObject* parent = nullptr) : QObject(parent) {}
  ~NativeActivation() override;
  bool start();
  void close();
 public slots:
  void Show();
  void EnsurePlayback();
 signals:
  void showRequested();
 private:
  bool started_ = false;
};
