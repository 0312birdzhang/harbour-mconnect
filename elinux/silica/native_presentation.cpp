#include "native_presentation.h"

#include <QCloseEvent>
#include <QUrl>

NativePresentation::NativePresentation(QObject* parent) : QObject(parent) {
  idle_.setSingleShot(true);
  connect(&poll_, &QTimer::timeout, this, &NativePresentation::evaluate);
  connect(&idle_, &QTimer::timeout, this, [this]() {
    // Work or a new lease may arrive after the last poll, including inside a
    // provider's nested event loop. Inspect authoritative state at the deadline.
    if (canExit()) emit idle();
  });
}

void NativePresentation::attach(QQuickView& view) {
  if (view_) view_->removeEventFilter(this);
  view_ = &view;
  view_->installEventFilter(this);
}

void NativePresentation::monitor(int idleMs, int pollMs) {
  idle_.setInterval(idleMs);
  poll_.start(pollMs);
  evaluate();
}

bool NativePresentation::canExit() const {
  return !requested && (!keepAlive || !keepAlive());
}

void NativePresentation::evaluate() {
  if (!canExit()) idle_.stop();
  else if (!idle_.isActive()) idle_.start();
}

void NativePresentation::Show() {
  requested = true;
  ++generation_;
  idle_.stop();
  if (view_ && (!prepare || prepare())) {
    view_->showFullScreen();
    view_->raise();
    view_->requestActivate();
  }
}

bool NativePresentation::eventFilter(QObject* watched, QEvent* event) {
  if (watched != view_ || event->type() != QEvent::Close) return QObject::eventFilter(watched, event);
  static_cast<QCloseEvent*>(event)->ignore();
  requested = false;
  view_->hide();
  const auto generation = ++generation_;
  // Release the QML tree after its close event returns. A foreground activation
  // in the meantime cancels release and keeps the new presentation attached.
  QTimer::singleShot(0, this, [this, generation]() {
    if (generation != generation_ || requested || !view_) return;
    view_->setSource(QUrl());
    emit presentationClosed();
    evaluate();
  });
  return true;
}
