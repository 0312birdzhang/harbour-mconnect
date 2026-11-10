#include "../elinux/silica/native_presentation.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QPointer>
#include <QQuickItem>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "PRESENTATION_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
void wait(int duration) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (elapsed.elapsed() < duration) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
}
}

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  QFile qml(temporary.path() + "/main.qml");
  CHECK(qml.open(QIODevice::WriteOnly));
  qml.write("import QtQuick 2.0\nItem { property string owner: core.objectName }\n");
  qml.close();
  QObject core;
  core.setObjectName("same-core");
  QQuickView view;
  view.rootContext()->setContextProperty("core", &core);
  NativePresentation presentation;
  presentation.attach(view);
  bool work = false;
  int leases = 1, idle = 0, closed = 0, prepared = 0;
  presentation.keepAlive = [&]() { return work || leases > 0; };
  presentation.prepare = [&]() {
    if (view.source().isEmpty()) { view.setSource(QUrl::fromLocalFile(qml.fileName())); ++prepared; }
    return view.status() != QQuickView::Error;
  };
  QObject::connect(&presentation, &NativePresentation::idle, [&]() { ++idle; });
  QObject::connect(&presentation, &NativePresentation::presentationClosed, [&]() { ++closed; });
  presentation.monitor(30, 5);
  presentation.Show();
  view.hide();
  CHECK(presentation.requested && view.rootObject());
  QPointer<QObject> original(view.rootObject());
  CHECK(!view.close());
  wait(80);
  CHECK(!presentation.requested && !original && !view.rootObject() && closed == 1 && idle == 0);
  presentation.Show();
  view.hide();
  CHECK(prepared == 2 && view.rootObject()->property("owner") == "same-core");
  QPointer<QObject> reopened(view.rootObject());
  CHECK(!view.close());
  presentation.Show();
  view.hide();
  wait(20);
  CHECK(presentation.requested && reopened && closed == 1 && prepared == 2);
  CHECK(!view.close());
  work = true;
  leases = 0;
  wait(80);
  CHECK(closed == 2 && idle == 0);
  work = false;
  wait(80);
  CHECK(idle > 0);
  // Make new activity arrive between an idle poll and its exit callback.
  for (bool lease : {false, true}) {
    NativePresentation deferred;
    bool pendingWork = false;
    int pendingLeases = 0, exits = 0;
    deferred.keepAlive = [&]() { return pendingWork || pendingLeases > 0; };
    QObject::connect(&deferred, &NativePresentation::idle, [&]() { ++exits; });
    deferred.monitor(25, 500);
    QTimer::singleShot(10, &deferred, [&]() { pendingWork = !lease; pendingLeases = lease ? 1 : 0; });
    wait(65);
    CHECK(exits == 0);
  }
  puts("NATIVE_PRESENTATION_PASS close-event qml-release same-core reopen deferred-close hidden-lease work idle deadline-recheck");
  return 0;
}
