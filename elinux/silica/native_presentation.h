#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickView>
#include <QTimer>
#include <functional>

class NativePresentation : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.sailfishos.Mconnect.Silica")
 public:
  explicit NativePresentation(QObject* parent = nullptr);
  void attach(QQuickView& view);
  void monitor(int idleMs = 10000, int pollMs = 500);
  bool requested = false;
  std::function<bool()> prepare;
  std::function<bool()> keepAlive;
 public slots:
  void Show();
 signals:
  void presentationClosed();
  void idle();
 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
 private:
  QPointer<QQuickView> view_;
  QTimer poll_, idle_;
  quint64 generation_ = 0;
  bool canExit() const;
  void evaluate();
};
