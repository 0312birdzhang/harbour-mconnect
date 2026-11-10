#include "native_activation.h"

#include <QDBusConnection>

NativeActivation::~NativeActivation() { close(); }

bool NativeActivation::start() {
  if (started_) return true;
  auto bus = QDBusConnection::sessionBus();
  if (!bus.registerObject("/org/sailfishos/Mconnect", this, QDBusConnection::ExportAllSlots)) return false;
  if (!bus.registerService("org.sailfishos.Mconnect")) {
    bus.unregisterObject("/org/sailfishos/Mconnect");
    return false;
  }
  started_ = true;
  return true;
}

void NativeActivation::close() {
  if (!started_) return;
  auto bus = QDBusConnection::sessionBus();
  bus.unregisterService("org.sailfishos.Mconnect");
  bus.unregisterObject("/org/sailfishos/Mconnect");
  started_ = false;
}

void NativeActivation::Show() { emit showRequested(); }
void NativeActivation::EnsurePlayback() {}
