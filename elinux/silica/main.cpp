#include <QGuiApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQmlProperty>
#include <QDBusConnection>
#include <QDBusMessage>
#include <sailfishapp.h>
#include <memory>
#include <cstdio>
#include <csignal>
#include <sys/stat.h>
#include <QTimer>
#include <QImage>
#include <QBuffer>
#include <QElapsedTimer>
#include <QAbstractItemModel>
#include <QPersistentModelIndex>
#include <QJsonDocument>
#include <QMap>
#include "bridge.h"
#include "native_activation.h"
#include "native_mpris.h"
#include "native_platforms.h"
#include "native_presentation.h"
#include "native_runtime.h"
#include "sailplayprovider.h"

namespace {
volatile std::sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }
bool ValidateHeaderGeometry(QGuiApplication& app, QObject* page, QQuickRenderControl& layout) {
  auto* header = page->findChild<QQuickItem*>("appHeader");
  auto* titles = page->findChild<QQuickItem*>("headerTitles");
  auto* settings = page->findChild<QQuickItem*>("headerSettingsButton");
  if (!header || !titles || !settings ||
      QQmlProperty(header, "headerPage").read().value<QQuickItem*>() != page) return false;

  const double padding = page->property("pagePadding").toDouble();
  const double margin = page->property("horizontalMargin").toDouble();
  const double toolWidth = page->property("toolWidth").toDouble();
  for (int orientation = 0; orientation < 4; ++orientation) {
    page->setProperty("testOrientation", orientation);
    const int expectedOrientation = page->property("expectedOrientation").toInt();
    page->setProperty("orientation", expectedOrientation);
    double titleHeight = 0;
    for (const auto& description : {QString(), QStringLiteral("测试歌手")}) {
      header->setProperty("description", description);
      app.processEvents();
      layout.polishItems();
      app.processEvents();
      const double inset = page->property("expectedInset").toDouble();
      if (page->property("orientation").toInt() != expectedOrientation ||
          header->property("description").toString() != description ||
          qAbs(header->property("topInset").toDouble() - inset) > 1 ||
          qAbs(header->height() - titles->height() - 2 * padding - inset) > 1 ||
          qAbs(titles->y() - padding - inset) > 1 ||
          qAbs(titles->x() - margin) > 1 ||
          qAbs(titles->width() + titles->x() + margin + toolWidth - header->width()) > 1 ||
          titles->width() <= 0 ||
          settings->y() + 1 < inset ||
          settings->y() + settings->height() > header->height() + 1 ||
          !(page->property("cutoutMode").toInt() & page->property("landscapeCutoutMode").toInt())) {
        qCritical("SILICA_HEADER_CUTOUT_FAIL orientation=%d inset=%f header=%f titlesX=%f titlesY=%f settingsY=%f",
            orientation, inset, header->height(), titles->x(), titles->y(), settings->y());
        return false;
      }
      if (description.isEmpty()) titleHeight = titles->height();
      else if (titles->height() <= titleHeight) {
        qCritical("SILICA_HEADER_CUTOUT_FAIL description-height");
        return false;
      }
      fprintf(stderr, "SILICA_HEADER_CUTOUT_GEOMETRY orientation=%d description=%d inset=%g header=%g titlesX=%g titlesY=%g settingsY=%g\n",
          orientation, !description.isEmpty(), inset, header->height(), titles->x(), titles->y(), settings->y());
    }
  }
  fprintf(stderr, "SILICA_HEADER_CUTOUT_PASS four-orientations title-description settings native-page horizontal-margin cutout=%g\n",
      page->property("screenCutoutHeight").toDouble());
  return true;
}
bool ValidateAboutContent(QGuiApplication& app, QQuickRenderControl& layout, QQuickItem* page) {
  app.processEvents();
  layout.polishItems();
  auto* body = page->findChild<QQuickItem*>("aboutContent");
  auto* identity = page->findChild<QQuickItem*>("aboutIdentity");
  auto* details = page->findChild<QQuickItem*>("aboutDetails");
  auto* scroll = page->findChild<QQuickItem*>("aboutScroll");
  auto* footer = page->findChild<QQuickItem*>("appFooter");
  auto* icon = page->findChild<QQuickItem*>("aboutIcon");
  auto* name = page->findChild<QObject*>("aboutName");
  auto* version = page->findChild<QObject*>("aboutVersion");
  auto* developer = page->findChild<QObject*>("aboutDeveloper");
  auto* disclaimer = page->findChild<QQuickItem*>("aboutDisclaimer");
  auto* disclaimerText = page->findChild<QQuickItem*>("aboutDisclaimerText");
  const QString expectedDisclaimer = QStringLiteral(
      "本项目为个人学习与自用开发的第三方客户端，与网易云音乐、QQ 音乐、酷狗音乐官方无任何关联。\n\n"
      "不提供、不存储、不分发任何音乐内容；所有内容版权归各平台与版权方所有。\n\n"
      "登录凭据只保存在本机（系统安全存储），仓库与日志中不含任何真实账号信息。\n\n"
      "接口随时可能调整，功能以实际可用为准。使用产生的任何后果由使用者自行承担。");
  if (!body || !identity || !details || !scroll || !footer || !icon || !name || !version ||
      !developer || !disclaimer || !disclaimerText ||
      disclaimerText->property("text").toString() != expectedDisclaimer ||
      disclaimerText->property("lineCount").toInt() <= 4 ||
      disclaimerText->width() <= 0 || disclaimerText->height() <= 0 ||
      icon->property("status").toInt() != 1 || icon->width() <= 0 ||
      name->property("text") != "Mconnect" || app.applicationVersion().isEmpty() ||
      version->property("text") != QStringLiteral("版本 ") + app.applicationVersion() ||
      developer->property("text") != "BirdZhang") {
    qCritical("SILICA_ABOUT_CONTENT_FAIL metadata-or-icon");
    return false;
  }
  QList<QQuickItem*> references;
  for (auto* item : details->childItems()) {
    if (item->objectName() == "aboutReference") references.append(item);
  }
  QMap<QString, QString> projectUrls = {
      {"Mconnect-Music_connect", "https://github.com/Hjdd14/Mconnect-Music_connect"},
      {"BiliMusic", "https://github.com/eason919191/BiliMusic"}};
  if (references.size() != projectUrls.size()) {
    qCritical("SILICA_ABOUT_CONTENT_FAIL reference-count=%d", references.size());
    return false;
  }
  for (auto* reference : references) {
    const QString projectName = reference->property("projectName").toString();
    const QString expectedUrl = projectUrls.take(projectName);
    const QString actualUrl = reference->property("projectUrl").toUrl().toString();
    if (expectedUrl.isEmpty() || actualUrl != expectedUrl ||
        reference->width() <= 0 || reference->height() <= 0) {
      qCritical("SILICA_ABOUT_CONTENT_FAIL reference=%s url=%s size=%gx%g",
          qPrintable(projectName), qPrintable(actualUrl), reference->width(), reference->height());
      return false;
    }
  }
  const QRectF identityRect = identity->mapRectToItem(body, QRectF(0, 0, identity->width(), identity->height()));
  const QRectF detailsRect = details->mapRectToItem(body, QRectF(0, 0, details->width(), details->height()));
  const QRectF disclaimerRect = disclaimer->mapRectToItem(body,
      QRectF(0, 0, disclaimer->width(), disclaimer->height()));
  const bool landscape = page->property("isLandscape").toBool();
  if (!scroll->clip() || identityRect.intersects(detailsRect) ||
      identityRect.width() <= 0 || identityRect.right() > body->width() + 1 ||
      detailsRect.right() > body->width() + 1 ||
      (landscape ? detailsRect.left() < identityRect.right() : detailsRect.top() < identityRect.bottom()) ||
      qAbs(disclaimerRect.left()) > 1 || qAbs(disclaimerRect.width() - body->width()) > 1 ||
      disclaimerRect.top() < qMax(identityRect.bottom(), detailsRect.bottom()) ||
      scroll->property("contentHeight").toDouble() + 1 < body->y() + disclaimerRect.bottom() ||
      qAbs(scroll->height() + (footer->isVisible() ? footer->height() : 0) - page->height()) > 1) {
    qCritical("SILICA_ABOUT_CONTENT_FAIL geometry orientation=%d", page->property("orientation").toInt());
    return false;
  }
  const double previousY = scroll->property("contentY").toDouble();
  scroll->setProperty("contentY", qMax(0.0, scroll->property("contentHeight").toDouble() - scroll->height()));
  app.processEvents();
  layout.polishItems();
  const QRectF textRect = disclaimerText->mapRectToItem(scroll,
      QRectF(0, 0, disclaimerText->width(), disclaimerText->height()));
  scroll->setProperty("contentY", previousY);
  if (textRect.bottom() > scroll->height() + 1 || textRect.bottom() <= 0) {
    qCritical("SILICA_ABOUT_CONTENT_FAIL disclaimer-scroll orientation=%d bottom=%g viewport=%g",
        page->property("orientation").toInt(), textRect.bottom(), scroll->height());
    return false;
  }
  fprintf(stderr, "SILICA_ABOUT_CONTENT orientation=%d landscape=%d version=%s viewport=%gx%g content=%g columns=%g,%g\n",
      page->property("orientation").toInt(), landscape, qPrintable(app.applicationVersion()),
      scroll->width(), scroll->height(), scroll->property("contentHeight").toDouble(),
      identityRect.width(), detailsRect.width());
  fprintf(stderr, "SILICA_ABOUT_DISCLAIMER orientation=%d paragraphs=4 lines=%d bottom=%g viewport=%g\n",
      page->property("orientation").toInt(), disclaimerText->property("lineCount").toInt(),
      textRect.bottom(), scroll->height());
  return true;
}
bool ValidateSearchSelectors(QObject* page) {
  auto* row = page->findChild<QQuickItem*>("searchSelectors");
  auto* platform = page->findChild<QQuickItem*>("searchPlatform");
  auto* mode = page->findChild<QQuickItem*>("searchMode");
  auto* field = page->findChild<QQuickItem*>("searchField");
  if (!row || !platform || !mode || !field || row->width() <= 0 ||
      platform->height() <= 0 || mode->height() <= 0 ||
      qAbs(platform->width() + mode->width() - row->width()) > 1 ||
      qAbs(mode->x() - platform->width()) > 1) return false;

  const QString query = QStringLiteral("组合框测试关键词");
  page->setProperty("queryText", query);
  platform->setProperty("currentIndex", 3);
  for (int index = 0; index < 4; ++index) {
    page->setProperty("hasMore", true);
    page->setProperty("currentPage", 5);
    page->setProperty("activeQuery", "previous-query");
    const int generation = page->property("generation").toInt();
    platform->setProperty("currentIndex", index);
    if (page->property("platformIndex").toInt() != index ||
        page->property("cachePlatform").toInt() != index ||
        page->property("queryText").toString() != query ||
        page->property("hasMore").toBool() ||
        page->property("currentPage").toInt() != 0 ||
        !page->property("activeQuery").toString().isEmpty() ||
        page->property("generation").toInt() <= generation) return false;
  }
  mode->setProperty("currentIndex", 1);
  for (int index = 0; index < 2; ++index) {
    const int generation = page->property("generation").toInt();
    mode->setProperty("currentIndex", index);
    if (page->property("modeIndex").toInt() != index ||
        page->property("queryText").toString() != query ||
        field->property("placeholderText").toString() !=
            (index == 0 ? QStringLiteral("搜索歌曲") : QStringLiteral("搜索歌单")) ||
        page->property("generation").toInt() <= generation) return false;
  }
  platform->setProperty("currentIndex", 0);
  mode->setProperty("currentIndex", 0);
  page->setProperty("queryText", "");
  fprintf(stderr, "SILICA_SEARCH_SELECTORS_PASS four-platforms two-types reset-pagination retain-query\n");
  return true;
}
bool ValidateSearchGeometry(QQuickItem* page) {
  auto* list = page->findChild<QQuickItem*>("searchResults");
  auto* controls = page->findChild<QQuickItem*>("searchControls");
  auto* selectors = page->findChild<QQuickItem*>("searchSelectors");
  auto* field = page->findChild<QQuickItem*>("searchField");
  auto* button = page->findChild<QQuickItem*>("searchButton");
  auto* placeholder = page->findChild<QQuickItem*>("searchPlaceholder");
  if (!list || !controls || !selectors || !field || !button || !placeholder ||
      !list->clip() || !placeholder->isEnabled()) return false;

  const auto bounds = [list](QQuickItem* item) {
    return item->mapRectToItem(list, QRectF(0, 0, item->width(), item->height()));
  };
  const QRectF controlsRect = bounds(controls);
  const QRectF selectorsRect = bounds(selectors);
  const QRectF fieldRect = bounds(field);
  const QRectF buttonRect = bounds(button);
  const QRectF placeholderRect = bounds(placeholder);
  if (selectorsRect.intersects(fieldRect) || selectorsRect.intersects(buttonRect) ||
      fieldRect.intersects(buttonRect) || placeholderRect.top() < controlsRect.bottom() - 1 ||
      placeholderRect.bottom() > list->height() + 1 || placeholderRect.left() < -1 ||
      placeholderRect.right() > list->width() + 1 || fieldRect.width() <= 0 ||
      buttonRect.right() > list->width() + 1 || fieldRect.right() > list->width() + 1) {
    qCritical("SILICA_SEARCH_LAYOUT_FAIL landscape=%d viewport=%gx%g controlsBottom=%g placeholder=%g..%g field=%g..%g button=%g..%g",
        page->property("isLandscape").toBool(), list->width(), list->height(),
        controlsRect.bottom(), placeholderRect.top(), placeholderRect.bottom(),
        fieldRect.left(), fieldRect.right(), buttonRect.left(), buttonRect.right());
    return false;
  }
  fprintf(stderr, "SILICA_SEARCH_LAYOUT landscape=%d mode=%d emptyQuery=%d viewport=%gx%g controlsBottom=%g placeholder=%g..%g\n",
      page->property("isLandscape").toBool(), page->property("modeIndex").toInt(),
      page->property("activeQuery").toString().isEmpty(), list->width(), list->height(),
      controlsRect.bottom(), placeholderRect.top(), placeholderRect.bottom());
  return true;
}
bool ValidateFooterGeometry(QGuiApplication& app, QQuickRenderControl& layout, QQuickItem* footer) {
  auto* backdrop = footer->findChild<QQuickItem*>("footerBackdrop");
  auto* row = footer->findChild<QQuickItem*>("navigationRow");
  auto* capsule = footer->findChild<QQuickItem*>("navigationCapsule");
  auto* mini = footer->findChild<QQuickItem*>("footerMiniPlayer");
  if (!backdrop || !row || !capsule || !mini || !footer->clip() ||
      backdrop->property("color").value<QColor>().alpha() != 255) return false;

  const bool originalVisibility = mini->isVisible();
  const double padding = footer->property("bottomPadding").toDouble();
  for (bool visible : {false, true}) {
    mini->setVisible(visible);
    app.processEvents();
    layout.polishItems();
    app.processEvents();
    const QPointF position = row->mapToItem(footer, QPointF());
    const double gap = footer->height() - position.y() - row->height();
    int buttons = 0;
    for (auto* button : row->childItems()) {
      if (button->objectName() != "navigationButton") continue;
      if (qAbs(button->width() * 4 - row->width()) > 1 ||
          qAbs(button->height() - row->height()) > 1 || button->y() != 0) return false;
      ++buttons;
    }
    if (buttons != 4 || padding <= 0 || row->width() <= 0 || row->height() <= 0 ||
        qAbs(gap - padding) > 1 || qAbs(position.y() - (visible ? mini->height() : 0)) > 1 ||
        qAbs(capsule->x() - position.x()) > 1 || qAbs(capsule->width() - row->width()) > 1 ||
        qAbs(capsule->y() + capsule->height() / 2 - position.y() - row->height() / 2) > 1) {
      qCritical("SILICA_FOOTER_GEOMETRY_FAIL mini=%d height=%g rowY=%g gap=%g padding=%g buttons=%d",
          visible, footer->height(), position.y(), gap, padding, buttons);
      return false;
    }
    fprintf(stderr, "SILICA_FOOTER_GEOMETRY mini=%d height=%g rowY=%g bottomPadding=%g buttons=%d\n",
        visible, footer->height(), position.y(), gap, buttons);
  }
  mini->setVisible(originalVisibility);
  app.processEvents();
  layout.polishItems();
  return true;
}
bool ValidatePlayerGeometry(QQuickItem* page) {
  auto* header = page->findChild<QQuickItem*>("appHeader");
  auto* lyrics = page->findChild<QQuickItem*>("playerLyrics");
  auto* artwork = page->findChild<QQuickItem*>("playerArtwork");
  auto* pane = page->findChild<QQuickItem*>("playerControlPane");
  auto* controls = page->findChild<QQuickItem*>("playerControls");
  auto* buttons = page->findChild<QQuickItem*>("playbackButtons");
  auto* scroll = page->findChild<QQuickItem*>("playerPageFlickable");
  auto* menu = page->findChild<QQuickItem*>("playerMenu");
  if (!header || !lyrics || !artwork || !pane || !controls || !buttons || !scroll || !menu ||
      QQmlProperty(menu, "flickable").read().value<QQuickItem*>() != scroll ||
      QQmlProperty(menu, "_page").read().value<QQuickItem*>() != page ||
      QQmlProperty(lyrics, "pullDownMenu").read().value<QQuickItem*>() ||
      qAbs(scroll->width() - page->width()) > 1 || qAbs(scroll->height() - page->height()) > 1 ||
      qAbs(menu->width() - page->width()) > 1 || scroll->y() != 0 || !scroll->clip()) return false;
  const QPointF artworkPosition = artwork->mapToItem(lyrics, QPointF());
  if (lyrics->height() <= 0 || pane->height() <= 0 || controls->height() <= 0 ||
      qAbs(lyrics->y() - header->height()) > 1 || pane->y() + 1 < header->height() ||
      qAbs(pane->y() + pane->height() - page->height()) > 1 ||
      qAbs(controls->width() - pane->width()) > 1 ||
      controls->y() < 0 || controls->y() + controls->height() > qMax(pane->height(), pane->property("contentHeight").toDouble()) + 1 ||
      buttons->x() < 0 || buttons->x() + buttons->width() > controls->width() + 1 ||
      artwork->width() <= 0 || artworkPosition.x() < -1 || artworkPosition.y() < -1 ||
      artworkPosition.x() + artwork->width() > lyrics->width() + 1 ||
      artworkPosition.y() + artwork->height() > lyrics->height() + 1 || !pane->clip()) return false;
  if (page->property("isLandscape").toBool()) {
    if (qAbs(lyrics->width() + pane->width() - page->width()) > 1 ||
        qAbs(lyrics->x() + lyrics->width() - pane->x()) > 1 ||
        qAbs(pane->y() - header->height()) > 1) return false;
  } else if (qAbs(lyrics->width() - page->width()) > 1 ||
      qAbs(pane->width() - page->width()) > 1 ||
      lyrics->y() + lyrics->height() > pane->y() + 1) return false;
  fprintf(stderr, "SILICA_PLAYER_GEOMETRY orientation=%d lyrics=%d page=%gx%g media=%gx%g controls=%gx%g artwork=%g\n",
      page->property("orientation").toInt(), page->property("showLyrics").toBool(),
      page->width(), page->height(), lyrics->width(), lyrics->height(),
      pane->width(), pane->height(), artwork->width());
  return true;
}
bool ValidatePlayerMenu(QGuiApplication& app, QQuickWindow& window,
                        QQuickRenderControl& layout, QQuickItem* page) {
  auto* header = page->findChild<QQuickItem*>("appHeader");
  auto* scroll = page->findChild<QQuickItem*>("playerPageFlickable");
  auto* menu = page->findChild<QQuickItem*>("playerMenu");
  if (!header || !scroll || !menu) return false;
  // A hidden validator has no foreground application state. Allow only this
  // isolated menu to react while delivering real pointer events to its window.
  const bool activeAllowed = menu->property("_activeAllowed").toBool();
  menu->setProperty("_activeAllowed", true);
  const QPointF start(header->width() / 2, header->height() / 2);
  const double distance = qMin(page->height() * 0.6, page->height() - start.y() - 20);
  unsigned long timestamp = 100;
  const auto pointer = [&](QEvent::Type type, double offset) {
    const QPointF position = page->mapToItem(window.contentItem(), start + QPointF(0, offset));
    QMouseEvent event(type, position, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    event.setTimestamp(timestamp);
    timestamp += 30;
    QCoreApplication::sendEvent(&window, &event);
    app.processEvents();
    layout.polishItems();
  };
  pointer(QEvent::MouseButtonPress, 0);
  for (int step = 1; step <= 12; ++step) pointer(QEvent::MouseMove, distance * step / 12);
  const bool opened = menu->property("active").toBool() &&
      scroll->property("contentY").toDouble() < scroll->property("originY").toDouble();
  const double contentY = scroll->property("contentY").toDouble();
  pointer(QEvent::MouseMove, 0);
  pointer(QEvent::MouseButtonRelease, 0);
  QMetaObject::invokeMethod(menu, "hide");
  QElapsedTimer timer;
  timer.start();
  while ((menu->property("active").toBool() || scroll->property("moving").toBool()) && timer.elapsed() < 2000) {
    app.processEvents();
    layout.polishItems();
  }
  menu->setProperty("_activeAllowed", activeAllowed);
  if (!opened || menu->property("active").toBool()) {
    qCritical("SILICA_PLAYER_MENU_GESTURE_FAIL orientation=%d opened=%d contentY=%g active=%d",
        page->property("orientation").toInt(), opened, contentY, menu->property("active").toBool());
    return false;
  }
  fprintf(stderr, "SILICA_PLAYER_MENU_GESTURE orientation=%d pageWidth=%g contentY=%g page-top-pointer-drag\n",
      page->property("orientation").toInt(), menu->width(), contentY);
  return true;
}
bool ValidateDownloadRows(QObject* page) {
  auto* model = page->findChild<QAbstractItemModel*>("downloadTaskModel");
  if (!model) return false;
  const auto task = [](const QString& id, double progress) {
    return QVariantMap{{"id",id},{"title",id},{"artist",""},{"quality","标准"},
      {"status","downloading"},{"progress",progress},{"cache",false},{"failure",""}};
  };
  const auto apply = [page](const QVariantList& rows) {
    const QVariant value = QVariantMap{{"tasks",rows},{"paused",false},{"blocked",""}};
    return QMetaObject::invokeMethod(page,"applySnapshot",Q_ARG(QVariant,value));
  };
  if (!apply({task("a",0.1),task("b",0.2)}) || model->rowCount()!=2) return false;
  QPersistentModelIndex original(model->index(0,0));
  if (!apply({task("b",0.2),task("a",0.5)}) || !original.isValid() || original.row()!=1) return false;
  int role = -1;
  const auto roles = model->roleNames();
  for (auto it=roles.begin();it!=roles.end();++it) if (it.value()=="rowJson") role=it.key();
  if (role<0) return false;
  const auto record=QJsonDocument::fromJson(model->data(original,role).toString().toUtf8()).toVariant().toMap();
  if (record.value("id")!="a" || record.value("progress").toDouble()!=0.5) return false;
  if (!apply({task("b",0.2)}) || model->rowCount()!=1 || original.isValid()) return false;
  fprintf(stderr,"SILICA_DOWNLOAD_ROWS_PASS insert-update-move-remove\n");
  return true;
}
}

int main(int argc, char** argv) {
  umask(0077);
  // Direct and D-Bus launches may bypass the invoker's Qt environment.
  // Select the phone compositor and input context before loading plugins.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "wayland");
  if (qEnvironmentVariableIsEmpty("QT_IM_MODULE")) qputenv("QT_IM_MODULE", "Maliit");
  std::unique_ptr<QGuiApplication> application(SailfishApp::application(argc, argv));
  auto& app = *application;
  app.setQuitOnLastWindowClosed(false);
  std::signal(SIGTERM, stop);
  std::signal(SIGINT, stop);
  QTimer stopTimer;
  QObject::connect(&stopTimer, &QTimer::timeout, &app, [&app]() {
    if (stopping) app.quit();
  });
  stopTimer.start(100);
  app.setApplicationName("Mconnect");
  app.setApplicationVersion(QStringLiteral(MCONNECT_VERSION));
  app.setOrganizationName("org.sailfishos");
  const bool validate = app.arguments().contains("--validate-qml");
  const bool background = app.arguments().contains("--background");
  if (validate) {
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
      fprintf(stderr, "%s\n", qPrintable(message));
    });
  }
  const int validateIndex = app.arguments().indexOf("--validate-qml");
  const QString qmlDirectory = validate && app.arguments().size() > validateIndex + 1
      ? app.arguments().at(validateIndex + 1) : "/usr/share/harbour-mconnect/qml";
  auto bus = QDBusConnection::sessionBus();
  NativeActivation activation;
  SailplayProvider provider(bus, "harbour-mconnect", {}, {"media"});
  NativePresentation control;
  // Export the SDK object before acquiring the activatable application name.
  // Cold-start clients can open their lease as soon as ownership is announced.
  if (!validate && !provider.start()) return 1;
  QObject::connect(&activation, &NativeActivation::showRequested, &control, &NativePresentation::Show);
  if (!validate && !activation.start()) {
    auto call = QDBusMessage::createMethodCall("org.sailfishos.Mconnect", "/org/sailfishos/Mconnect",
        "org.sailfishos.Mconnect", background ? "EnsurePlayback" : "Show");
    call.setAutoStartService(false);
    return bus.call(call, QDBus::Block, 5000).type() == QDBusMessage::ReplyMessage ? 0 : 1;
  }
  if (!validate && !bus.registerService("org.sailfishos.Mconnect.Silica")) return 1;
  NativePlatforms platforms;
  if(!validate && !bus.registerObject("/org/sailfishos/Mconnect/Platforms",&platforms,QDBusConnection::ExportAllSlots))return 1;
  std::unique_ptr<NativeStorage> storage;
  std::unique_ptr<NativeCoreRuntime> runtime;
  if (!validate) {
    try {
      storage.reset(new NativeStorage);
      runtime.reset(new NativeCoreRuntime(*storage));
    } catch (const std::runtime_error&) { qCritical("MCONNECT_NATIVE_STORAGE_START_FAILED"); return 1; }
    if (!runtime->start()) { qCritical("MCONNECT_NATIVE_CORE_START_FAILED"); return 1; }
    qInfo("MCONNECT_NATIVE_CORE_STARTED requests=%d", runtime->requestNames().size());
  }
  CoreBridge bridge(nullptr, false);
  if(!validate && !bus.registerObject("/org/sailfishos/Mconnect/Artwork",bridge.artworkBackend(),QDBusConnection::ExportAllSlots))return 1;
  std::unique_ptr<QQuickRenderControl> validationLayout;
  std::unique_ptr<QQuickWindow> validationWindow;
  if (validate) {
    validationLayout.reset(new QQuickRenderControl);
    validationWindow.reset(new QQuickWindow(validationLayout.get()));
  }
  std::unique_ptr<QObject> validationCore;
  std::unique_ptr<QQuickView> window(SailfishApp::createView());
  auto& view = *window;
  if (validate) {
    QQmlComponent fixture(view.engine());
    fixture.setData(R"(
import QtQuick 2.0
QtObject {
    property bool ready: false
    property var state: ({})
    property var lyrics: ({})
    property string artwork: ""
    property string coverInk: "#ffffff"
    property var queue: []
    property var accounts: []
    property var login: ({})
    property int requestCount: 0
    property string lastRequest: ""
    signal completed(int id, var value, string error)
    function request(method, args) {
        var id = ++requestCount
        lastRequest = JSON.stringify({id: id, method: method, args: args})
        return id
    }
}
)", QUrl());
    validationCore.reset(fixture.create());
    if (!validationCore) return 1;
  }
  view.setTitle("Mconnect");
  view.setResizeMode(QQuickView::SizeRootObjectToView);
  view.rootContext()->setContextProperty("core", validate ? validationCore.get() : &bridge);
  view.rootContext()->setContextProperty("validationMode", validate);
  control.attach(view);
  control.prepare = [&app, &view, &bridge, qmlDirectory, validate]() {
    if (!validate) bridge.start();
    if (view.source().isEmpty()) view.setSource(QUrl::fromLocalFile(qmlDirectory + "/main.qml"));
    if (view.status() == QQuickView::Error) { app.exit(1); return false; }
    return true;
  };
  if (!validate && !bus.registerObject("/org/sailfishos/Mconnect/Silica", &control,
      QDBusConnection::ExportAllSlots)) return 1;
  if ((validate || !background || control.requested) && !control.prepare()) return 1;
  if (validate) {
    view.rootObject()->setParentItem(validationWindow->contentItem());
    // Login codes are memory-only PNG data URLs. Exercise Qt's actual loader
    // without presenting a window or saving an ordinary image to disk.
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    QImage pixel(1, 1, QImage::Format_RGB32);
    pixel.fill(Qt::white);
    if (!pixel.save(&buffer, "PNG")) return 1;
    QQmlComponent imageCheck(view.engine());
    imageCheck.setData("import QtQuick 2.0\nImage { source: \"data:image/png;base64," +
        png.toBase64() + "\" }", QUrl());
    std::unique_ptr<QObject> image(imageCheck.create());
    if (!image) return 1;
    QElapsedTimer elapsed; elapsed.start();
    while (image->property("status").toInt() == 2 && elapsed.elapsed() < 2000) {
      app.processEvents();
    }
    if (image->property("status").toInt() != 1) return 1;
    fprintf(stderr, "SILICA_IMAGE_DATA_URL_PASS\n");
    for (const char* file : {"GroupSlideshow.qml", "MainShellPage.qml", "AppHeader.qml", "AppFooter.qml", "PlatformTabs.qml", "PlaylistSourcesPage.qml", "SearchPage.qml", "PlayerPage.qml", "QueuePage.qml", "MiniPlayer.qml", "cover/CoverPage.qml", "LibraryPage.qml", "AccountsPage.qml", "LoginPage.qml", "LibraryPlaylistsPage.qml", "LibrarySongsPage.qml", "CreatePlaylistDialog.qml", "DiscoveryPage.qml", "ChartsPage.qml", "DownloadsPage.qml", "DownloadSongPage.qml", "SettingsPage.qml", "AboutPage.qml", "LocalMusicPage.qml", "ScanDirectoryDialog.qml", "SongActionsPage.qml", "ArtistPage.qml", "ArtistAlbumsPage.qml", "AlbumPage.qml", "AddToPlaylistPage.qml", "PlaylistTransferPage.qml"}) {
      if(app.arguments().contains("--shell-only"))break;
      QQmlComponent component(view.engine(), QUrl::fromLocalFile(
          qmlDirectory + "/" + file));
      if(QString::fromLatin1(file)=="DiscoveryPage.qml") {
        QVariantList rows;
        for(int i=0;i<9;++i)rows.append(QVariantMap{{"title",QStringLiteral("中文测试歌曲较长标题")},{"artist",QStringLiteral("测试歌手")}});
        component.setData("import QtQuick 2.0\nDiscoveryPage { songs:"+QJsonDocument::fromVariant(rows).toJson(QJsonDocument::Compact)+" }",QUrl::fromLocalFile(qmlDirectory+"/ValidationFixture.qml"));
      }
      if (QString::fromLatin1(file) == "AppHeader.qml") {
        component.setData(
            "import QtQuick 2.0\nimport Sailfish.Silica 1.0\n"
            "Page { visible: true; property int testOrientation: 0; "
            "property int expectedOrientation: [Orientation.Portrait, Orientation.Landscape, Orientation.PortraitInverted, Orientation.LandscapeInverted][testOrientation]; "
            "property real pagePadding: Theme.paddingLarge; "
            "property real horizontalMargin: Theme.horizontalPageMargin; "
            "property real toolWidth: Theme.itemSizeSmall; "
            "property real screenCutoutHeight: Screen.topCutout.height; "
            "property real expectedInset: expectedOrientation === Orientation.Portrait ? Screen.topCutout.height : 0; "
            "property int landscapeCutoutMode: CutoutMode.AvoidLandscapeCutout; "
            "Column { width: parent.width; AppHeader { title: \"测试标题\"; tools: true; showBack: true } } }",
            QUrl::fromLocalFile(qmlDirectory + "/ValidationFixture.qml"));
      }
      if (component.isError()) {
        for (const auto& error : component.errors()) qCritical("%s", qPrintable(error.toString()));
        return 1;
      }
      // DialogHeader requires live page-stack indicators; validate its type
      // here and exercise its instance through the real application instead.
      if (QString::fromLatin1(file).endsWith("Dialog.qml") || (QString::fromLatin1(file)=="MainShellPage.qml" || QString::fromLatin1(file)=="GroupSlideshow.qml")) continue;
      std::unique_ptr<QQuickRenderControl> headerLayout;
      std::unique_ptr<QQuickWindow> headerWindow;
      if (QString::fromLatin1(file) == "AppHeader.qml") {
        headerLayout.reset(new QQuickRenderControl);
        headerWindow.reset(new QQuickWindow(headerLayout.get()));
      }
      std::unique_ptr<QObject> object(component.beginCreate(QQmlEngine::contextForObject(view.rootObject())));
      if (!object) {
        for (const auto& error : component.errors()) qCritical("%s", qPrintable(error.toString()));
        return 1;
      }
      if (auto* item = qobject_cast<QQuickItem*>(object.get())) {
        item->setParentItem(headerWindow ? headerWindow->contentItem() : view.rootObject());
      }
      component.completeCreate();
      app.processEvents();
      validationLayout->polishItems();
      if (object->property("allowedOrientations").isValid() &&
          object->property("allowedOrientations").toInt() != 15) {
        qCritical("SILICA_PAGE_ORIENTATIONS_FAIL %s", file);
        return 1;
      }
      if (QString::fromLatin1(file) == "AppHeader.qml" && !ValidateHeaderGeometry(app, object.get(), *headerLayout)) return 1;
      if(QString::fromLatin1(file)=="AppFooter.qml") {
        auto* footer = qobject_cast<QQuickItem*>(object.get());
        if (!footer || !ValidateFooterGeometry(app, *validationLayout, footer)) return 1;
        fprintf(stderr,"SILICA_FOOTER_PASS opaque-backdrop clipped raised-buttons aligned-capsule\n");
      }
      if (QString::fromLatin1(file)=="SearchPage.qml") {
        app.processEvents();
        if (!ValidateSearchSelectors(object.get())) {
          qCritical("SILICA_SEARCH_SELECTORS_FAIL");
          return 1;
        }
      }
      if (QString::fromLatin1(file)=="DownloadsPage.qml" && !ValidateDownloadRows(object.get())) return 1;
      if(QString::fromLatin1(file)=="DiscoveryPage.qml") {
        app.processEvents();
        auto* grid=object->findChild<QQuickItem*>("recommendationGrid");
        auto* scroll=object->findChild<QQuickItem*>("discoveryScroll");
        auto* footer=object->findChild<QQuickItem*>("appFooter");
        int cards=0;
        if(grid)for(auto* item:grid->childItems())if(item->objectName()=="recommendationCard")++cards;
        if(!grid || !scroll || !footer || cards!=9 || grid->height()<=0 || scroll->property("contentHeight").toDouble()+1<grid->y()+grid->height() || qAbs(QQmlProperty(scroll,"anchors.bottomMargin").read().toDouble()-(footer->isVisible()?footer->height():0))>1) {
          qCritical("SILICA_DISCOVERY_GRID_FAIL grid=%p scroll=%p footer=%p count=%d gridHeight=%f content=%f y=%f scrollHeight=%f footerHeight=%f",static_cast<void*>(grid),static_cast<void*>(scroll),static_cast<void*>(footer),cards,grid?grid->height():-1,scroll?scroll->property("contentHeight").toDouble():-1,grid?grid->y():-1,scroll?scroll->height():-1,footer?footer->height():-1);return 1;
        }
        fprintf(stderr,"SILICA_DISCOVERY_GRID_PASS nine-cards scroll-footer\n");
      }
    }
    auto routeState=[&]() {
      QVariant result;
      QMetaObject::invokeMethod(view.rootObject(),"validationState",Q_RETURN_ARG(QVariant,result));
      return result.toMap();
    };
    const QStringList roots={"exploreRoot","discoveryRoot","libraryRoot","downloadsRoot"};
    auto settle = [&]() {
      QElapsedTimer timer;
      timer.start();
      QVariantMap state;
      do {
        app.processEvents();
        validationLayout->polishItems();
        state = routeState();
      } while ((state.value("busy").toBool() || state.value("page").toString().isEmpty() ||
          state.value("pane").toString() != roots.value(state.value("tab").toInt())) &&
          timer.elapsed() < 2000);
    };
    settle();
    for(int index=0;index<4;++index) {
      QVariant switched;
      QMetaObject::invokeMethod(view.rootObject(),"switchTab",Q_RETURN_ARG(QVariant,switched),Q_ARG(QVariant,QVariant(index)));
      settle();
      const auto state=routeState();
      if(!switched.toBool() || state.value("page").toString()!=roots[index] || state.value("depth").toInt()!=1 || !state.value("footer").toBool() || state.value("barHeight").toDouble()<=0) {
        qCritical("SILICA_NAVIGATION_FAIL %s",qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(state).toJson())));return 1;
      }
      if(index==2){QMetaObject::invokeMethod(view.rootObject(),"validationChild");settle();if(routeState().value("depth").toInt()!=2 || !routeState().value("footer").toBool())return 1;}
    }
    fprintf(stderr,"SILICA_NAVIGATION_PASS four-tabs nested-reset footer\n");
    for(int tab=0;tab<4;++tab) {
      QVariant switched;
      QMetaObject::invokeMethod(view.rootObject(),"switchTab",Q_RETURN_ARG(QVariant,switched),Q_ARG(QVariant,QVariant(tab)));settle();
      const int count=(tab==0 || tab==2)?1:tab==3?3:4;
      for(int slide=0;slide<count;++slide) {
        QVariant selected;
        QMetaObject::invokeMethod(view.rootObject(),"selectSlide",Q_RETURN_ARG(QVariant,selected),Q_ARG(QVariant,QVariant(slide)));
        QElapsedTimer timer;timer.start();
        do {app.processEvents();}while(routeState().value("pane").toString().isEmpty() && timer.elapsed()<2000);
        const auto state=routeState();
        if(!selected.toBool() || state.value("tab").toInt()!=tab || state.value("slide").toInt()!=slide || state.value("slideCount").toInt()!=count || state.value("pane").toString()!=roots[tab] || !state.value("sharedMenu").toBool() || (tab==1 && state.value("platform").toInt()!=slide) || (tab==3 && state.value("filter").toInt()!=slide)) {
          qCritical("SILICA_GROUP_SLIDESHOW_FAIL %s",qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(state).toJson())));return 1;
        }
      }
      QMetaObject::invokeMethod(view.rootObject(),"validationStep",Q_ARG(QVariant,QVariant(1)));app.processEvents();
      if(routeState().value("slide").toInt()!=0 || routeState().value("tab").toInt()!=tab)return 1;
      QMetaObject::invokeMethod(view.rootObject(),"validationStep",Q_ARG(QVariant,QVariant(-1)));app.processEvents();
      if(routeState().value("slide").toInt()!=count-1 || routeState().value("tab").toInt()!=tab)return 1;
      QVariant rejected;
      QMetaObject::invokeMethod(view.rootObject(),"selectSlide",Q_RETURN_ARG(QVariant,rejected),Q_ARG(QVariant,QVariant(count)));
      if(rejected.toBool() || routeState().value("tab").toInt()!=tab)return 1;
    }
    for(int tab:{0,1,2,3}) {
      QVariant switched;
      QMetaObject::invokeMethod(view.rootObject(),"switchTab",Q_RETURN_ARG(QVariant,switched),Q_ARG(QVariant,QVariant(tab)));settle();
      if(routeState().value("slide").toInt()!=((tab==0 || tab==2)?0:tab==3?2:3))return 1;
    }
    fprintf(stderr,"SILICA_GROUP_SLIDESHOW_PASS separate-1-4-1-3 shared-menu local-wrap no-cross-tab remembered-index\n");
    QMetaObject::invokeMethod(view.rootObject(), "switchTab", Q_ARG(QVariant, QVariant(0)));
    settle();
    auto* searchPane = QQmlProperty(view.rootObject(), "homePage.currentPane").read().value<QQuickItem*>();
    if (!searchPane) return 1;
    auto* searchPlatform = searchPane->findChild<QObject*>("searchPlatform");
    auto* searchMode = searchPane->findChild<QObject*>("searchMode");
    auto* searchField = searchPane->findChild<QObject*>("searchField");
    if (!searchPlatform || !searchMode || !searchField) {
      qCritical("SILICA_SEARCH_SINGLE_PANE_FAIL missing-selectors");
      return 1;
    }
    searchField->setProperty("text", QStringLiteral("保留搜索关键词"));
    searchPlatform->setProperty("currentIndex", 2);
    searchMode->setProperty("currentIndex", 1);
    QMetaObject::invokeMethod(view.rootObject(), "switchTab", Q_ARG(QVariant, QVariant(1)));
    settle();
    QMetaObject::invokeMethod(view.rootObject(), "switchTab", Q_ARG(QVariant, QVariant(0)));
    settle();
    searchPane = QQmlProperty(view.rootObject(), "homePage.currentPane").read().value<QQuickItem*>();
    if (!searchPane) return 1;
    searchMode = searchPane->findChild<QObject*>("searchMode");
    searchField = searchPane->findChild<QObject*>("searchField");
    if (!searchMode || !searchField || routeState().value("platform").toInt() != 2 ||
        routeState().value("slideCount").toInt() != 1 ||
        searchMode->property("currentIndex").toInt() != 1 ||
        searchField->property("text").toString() != QStringLiteral("保留搜索关键词")) {
      qCritical("SILICA_SEARCH_SINGLE_PANE_FAIL %s", qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(routeState()).toJson())));
      return 1;
    }
    fprintf(stderr, "SILICA_SEARCH_SINGLE_PANE_PASS remembered-platform type query\n");
    for(const char* method:{"openPlayer","openPlayer","openQueue","openQueue","openPlayer"}) {
      QMetaObject::invokeMethod(view.rootObject(),method);settle();
      const auto state=routeState();
      const bool player=QString::fromLatin1(method)=="openPlayer";
      if(state.value("depth").toInt()!=(player?2:3) || state.value("page").toString()!=(player?"playerPage":"queuePage"))return 1;
    }
    fprintf(stderr,"SILICA_UNIQUE_PLAYER_QUEUE_PASS repeated-open reuses-stack\n");
    auto* root = view.rootObject();
    validationWindow->resize(QSize(qRound(root->width()), qRound(root->height())));
    validationCore->setProperty("state", QVariantMap{
        {"song", QVariantMap{{"id", "validation-only-track"}, {"platform", "local"},
            {"title", QStringLiteral("隔离布局测试歌曲")}, {"artist", QStringLiteral("测试歌手")}}},
        {"repeat", "all"}, {"shuffle", false}, {"playing", false}, {"busy", false},
        {"position", 0}, {"duration", 180000}});
    validationCore->setProperty("ready", true);
    const int originalAllowed = root->property("allowedOrientations").toInt();
    const int originalDevice = root->property("deviceOrientation").toInt();
    if (root->property("_defaultPageOrientations").toInt() != 15 ||
        originalAllowed != root->property("defaultAllowedOrientations").toInt()) return 1;
    // Exercise every native orientation in this hidden validation process.
    // The application normally follows the system's allowed orientations.
    root->setProperty("allowedOrientations", 15);
    auto currentPage = [root]() {
      return QQmlProperty(root, "pageStack.currentPage").read().value<QQuickItem*>();
    };
    const auto validateDownloadDirectory = [&]() {
      QMetaObject::invokeMethod(root, "switchTab", Q_ARG(QVariant, QVariant(2)));
      settle();
      auto* library = QQmlProperty(root, "homePage.currentPane").read().value<QQuickItem*>();
      auto* settingsEntry = library ? library->findChild<QQuickItem*>("librarySettingsEntry") : nullptr;
      auto* pageStack = QQmlProperty(root, "pageStack").read().value<QObject*>();
      QObject* clickEvent = nullptr;
      qInfo("SILICA_DOWNLOAD_DIRECTORY_STEP entry=%p route=%s", static_cast<void*>(settingsEntry),
          qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(routeState()).toJson(QJsonDocument::Compact))));
      if (!settingsEntry || !pageStack) return false;
      if (!QMetaObject::invokeMethod(settingsEntry, "clicked",
          QGenericArgument("QQuickMouseEvent*", &clickEvent))) return false;
      settle();
      auto* settingsPage = currentPage();
      qInfo("SILICA_DOWNLOAD_DIRECTORY_STEP page=%s pending=%d ready=%d", qPrintable(settingsPage->objectName()),
          settingsPage->property("pending").toInt(), validationCore->property("ready").toBool());
      if (settingsPage->objectName() != "settingsPage" || !settingsPage->property("pending").toInt()) return false;
      const auto completeSettings = [&](const QVariantMap& value, const QString& error = QString()) {
        return QMetaObject::invokeMethod(validationCore.get(), "completed",
            Q_ARG(int, settingsPage->property("pending").toInt()),
            Q_ARG(QVariant, QVariant(value)), Q_ARG(QString, error));
      };
      if (!completeSettings({{"sizeLimitMb", 1024}, {"wifiOnly", true}, {"autoRetry", true},
          {"autoCleanup", true}, {"offlineMode", false}})) return false;
      const auto directoryRead = QJsonDocument::fromJson(
          validationCore->property("lastRequest").toString().toUtf8()).toVariant().toMap();
      qInfo("SILICA_DOWNLOAD_DIRECTORY_STEP loaded=%d pending=%d method=%s", settingsPage->property("loaded").toBool(),
          settingsPage->property("pending").toInt(), qPrintable(directoryRead.value("method").toString()));
      const QString initialDirectory = "/home/defaultuser/Downloads";
      if (directoryRead.value("method") != "downloadDirectory" ||
          !completeSettings({{"path", initialDirectory}})) return false;
      auto* directoryEntry = settingsPage->findChild<QQuickItem*>("downloadDirectoryEntry");
      qInfo("SILICA_DOWNLOAD_DIRECTORY_STEP selector=%p enabled=%d value=%s", static_cast<void*>(directoryEntry),
          directoryEntry && directoryEntry->property("enabled").toBool(),
          directoryEntry ? qPrintable(directoryEntry->property("value").toString()) : "");
      if (!directoryEntry || !directoryEntry->property("enabled").toBool() ||
          directoryEntry->property("value") != initialDirectory) return false;
      const int beforePicker = validationCore->property("requestCount").toInt();
      if (!QMetaObject::invokeMethod(directoryEntry, "clicked", QGenericArgument("QQuickMouseEvent*", &clickEvent))) return false;
      settle();
      qInfo("SILICA_DOWNLOAD_DIRECTORY_STEP picker=%s", qPrintable(currentPage()->objectName()));
      if (currentPage()->objectName() != "downloadFolderPicker" ||
          !currentPage()->property("selectedPath").toString().isEmpty() ||
          validationCore->property("requestCount").toInt() != beforePicker) return false;
      if (!QMetaObject::invokeMethod(pageStack, "pop",
          Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()))) return false;
      settle();
      if (currentPage() != settingsPage || settingsPage->property("downloadDirectory") != initialDirectory ||
          validationCore->property("requestCount").toInt() != beforePicker) return false;
      const QString chosenDirectory = "/home/defaultuser/Music/selected folder";
      if (!QMetaObject::invokeMethod(directoryEntry, "clicked", QGenericArgument("QQuickMouseEvent*", &clickEvent))) return false;
      settle();
      if (currentPage()->objectName() != "downloadFolderPicker" ||
          !currentPage()->setProperty("selectedPath", chosenDirectory)) return false;
      const auto directoryWrite = QJsonDocument::fromJson(
          validationCore->property("lastRequest").toString().toUtf8()).toVariant().toMap();
      if (directoryWrite.value("method") != "setDownloadDirectory" ||
          directoryWrite.value("args").toMap().value("path") != chosenDirectory ||
          directoryEntry->property("enabled").toBool()) return false;
      if (!QMetaObject::invokeMethod(pageStack, "pop",
          Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()))) return false;
      settle();
      if (!completeSettings({{"path", chosenDirectory}}) ||
          directoryEntry->property("value") != chosenDirectory ||
          !directoryEntry->property("enabled").toBool()) return false;
      if (!QMetaObject::invokeMethod(directoryEntry, "clicked", QGenericArgument("QQuickMouseEvent*", &clickEvent))) return false;
      settle();
      if (currentPage()->objectName() != "downloadFolderPicker" ||
          !currentPage()->setProperty("selectedPath", "/blocked/fixture")) return false;
      if (!QMetaObject::invokeMethod(pageStack, "pop",
          Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()))) return false;
      settle();
      const QString directoryError = QStringLiteral("无法写入所选文件夹");
      if (!completeSettings({}, directoryError) || settingsPage->property("errorText") != directoryError ||
          directoryEntry->property("value") != chosenDirectory ||
          !directoryEntry->property("enabled").toBool()) return false;
      if (!QMetaObject::invokeMethod(pageStack, "pop",
          Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()))) return false;
      settle();
      fprintf(stderr, "SILICA_DOWNLOAD_DIRECTORY_PASS settings-entry native-folder-picker cancel select pending save error-retains-directory\n");
      return true;
    };
    if (app.arguments().contains("--download-directory-only")) {
      if (!validateDownloadDirectory()) return 1;
      fprintf(stderr, "SILICA_DOWNLOAD_DIRECTORY_VALIDATION_PASS\n");
      return 0;
    }
    auto settleOrientation = [&](int orientation) {
      QElapsedTimer timer;
      timer.start();
      QQuickItem* page = nullptr;
      do {
        app.processEvents();
        validationLayout->polishItems();
        page = currentPage();
      } while ((!page || page->property("orientation").toInt() != orientation ||
          page->property("orientationTransitionRunning").toBool() ||
          QQmlProperty(root, "pageStack.busy").read().toBool()) && timer.elapsed() < 2000);
      return page && page->property("orientation").toInt() == orientation &&
          page->property("_windowOrientation").toInt() == orientation &&
          page->property("allowedOrientations").toInt() == 15 &&
          !page->property("orientationTransitionRunning").toBool() &&
          page->width() > 0 && page->height() > 0;
    };
    for (int orientation : {1, 2, 4, 8, 1}) {
      if (!root->setProperty("deviceOrientation", orientation) || !settleOrientation(orientation)) return 1;
      for (int tab = 0; tab < 4; ++tab) {
        QMetaObject::invokeMethod(root, "switchTab", Q_ARG(QVariant, QVariant(tab)));
        settle();
        if (!settleOrientation(orientation)) return 1;
        auto* page = currentPage();
        auto* footer = page->findChild<QQuickItem*>("appFooter");
        auto* pane = QQmlProperty(root, "homePage.currentPane").read().value<QQuickItem*>();
        auto* scroll = QQmlProperty(root, "homePage.outerFlickable").read().value<QQuickItem*>();
        if (!footer || !pane || !scroll || !ValidateFooterGeometry(app, *validationLayout, footer) ||
            pane->width() <= 0 || pane->height() <= 0 || pane->rotation() != 0 ||
            qAbs(pane->width() - page->width()) > 1 ||
            qAbs(scroll->height() + footer->height() - page->height()) > 1) {
          qCritical("SILICA_ROTATION_SHELL_FAIL orientation=%d tab=%d page=%gx%g pane=%gx%g paneRotation=%g scrollHeight=%g footerHeight=%g",
              orientation, tab, page->width(), page->height(), pane ? pane->width() : -1,
              pane ? pane->height() : -1, pane ? pane->rotation() : -1,
              scroll ? scroll->height() : -1, footer ? footer->height() : -1);
          return 1;
        }
        if (tab == 0) {
          auto* selectors = pane->findChild<QQuickItem*>("searchSelectors");
          auto* platform = pane->findChild<QQuickItem*>("searchPlatform");
          auto* mode = pane->findChild<QQuickItem*>("searchMode");
          if (!selectors || !platform || !mode || selectors->width() <= 0 ||
              qAbs(platform->width() + mode->width() - selectors->width()) > 1 ||
              qAbs(mode->x() - platform->width()) > 1) return 1;
          auto* mini = footer->findChild<QQuickItem*>("footerMiniPlayer");
          auto* placeholder = pane->findChild<QQuickItem*>("searchPlaceholder");
          if (!mini || !placeholder) return 1;
          const bool miniVisible = mini->isVisible();
          pane->setProperty("queryText", "");
          QMetaObject::invokeMethod(pane, "reset");
          for (bool visible : {false, true}) {
            mini->setVisible(visible);
            for (int searchMode : {0, 1}) {
              mode->setProperty("currentIndex", searchMode);
              for (const auto& query : {QString(), QStringLiteral("empty-result-query")}) {
                pane->setProperty("activeQuery", query);
                app.processEvents();
                validationLayout->polishItems();
                app.processEvents();
                if (!ValidateSearchGeometry(pane)) return 1;
              }
            }
            pane->setProperty("searching", true);
            if (placeholder->isEnabled()) return 1;
            pane->setProperty("searching", false);
            pane->setProperty("searchError", "isolated-search-error");
            if (placeholder->isEnabled()) return 1;
            pane->setProperty("searchError", "");
          }
          mini->setVisible(miniVisible);
          app.processEvents();
          validationLayout->polishItems();
        }
        fprintf(stderr, "SILICA_ROTATION_SHELL orientation=%d tab=%d page=%gx%g pane=%gx%g bottomPadding=%g\n",
            orientation, tab, page->width(), page->height(), pane->width(), pane->height(),
            footer->property("bottomPadding").toDouble());
      }
      QMetaObject::invokeMethod(root, "openPlayer");
      settle();
      if (!settleOrientation(orientation)) return 1;
      auto* player = currentPage();
      if (player->objectName() != "playerPage") return 1;
      for (bool showLyrics : {false, true}) {
        player->setProperty("showLyrics", showLyrics);
        app.processEvents();
        validationLayout->polishItems();
        if (!ValidatePlayerGeometry(player)) {
          qCritical("SILICA_PLAYER_GEOMETRY_FAIL orientation=%d lyrics=%d", orientation, showLyrics);
          return 1;
        }
        if (!ValidatePlayerMenu(app, *validationWindow, *validationLayout, player)) return 1;
      }
      const QString originalError = root->property("errorText").toString();
      root->setProperty("errorText", QString(2000, QChar(0x6d4b)));
      app.processEvents();
      validationLayout->polishItems();
      if (!ValidatePlayerGeometry(player)) return 1;
      auto* controls = player->findChild<QQuickItem*>("playerControls");
      auto* controlPane = player->findChild<QQuickItem*>("playerControlPane");
      if (controls->height() <= controlPane->height() ||
          controlPane->property("contentHeight").toDouble() + 1 < controls->height()) {
        qCritical("SILICA_PLAYER_OVERFLOW_FAIL orientation=%d content=%g viewport=%g",
            orientation, controls->height(), controlPane->height());
        return 1;
      }
      root->setProperty("errorText", originalError);
      QMetaObject::invokeMethod(root, "openQueue");
      settle();
      if (!settleOrientation(orientation) || currentPage()->objectName() != "queuePage") return 1;
    }
    const auto completeDownload = [&](int id, const QString& error) {
      return QMetaObject::invokeMethod(validationCore.get(), "completed", Q_ARG(int, id),
          Q_ARG(QVariant, QVariant(QVariantMap{{"id", "isolated-download-task"}})), Q_ARG(QString, error));
    };
    const auto openDownload = [&]() -> QQuickItem* {
      QMetaObject::invokeMethod(root, "openPlayer");
      settle();
      auto* action = currentPage()->findChild<QQuickItem*>("playerDownloadAction");
      if (!action || !action->property("enabled").toBool() || !QMetaObject::invokeMethod(action, "clicked")) return nullptr;
      settle();
      return currentPage()->objectName() == "downloadSongPage" ? currentPage() : nullptr;
    };
    const auto waitForDownloads = [&]() {
      QElapsedTimer timer;
      timer.start();
      bool visible = false;
      do {
        app.processEvents();
        validationLayout->polishItems();
        const auto state = routeState();
        visible = state.value("page").toString() == "downloadsRoot" &&
            state.value("pane").toString() == "downloadsRoot" && state.value("depth").toInt() == 1 &&
            state.value("tab").toInt() == 3 && state.value("filter").toInt() == 0 &&
            !state.value("busy").toBool() &&
            currentPage() == QQmlProperty(root, "homePage").read().value<QQuickItem*>();
      } while (!visible && timer.elapsed() < 2000);
      return visible;
    };
    for (int orientation : {2, 8, 1}) {
      root->setProperty("deviceOrientation", orientation);
      if (!settleOrientation(orientation)) return 1;
      QMetaObject::invokeMethod(root, "switchTab", Q_ARG(QVariant, QVariant(3)));
      settle();
      QMetaObject::invokeMethod(root, "selectSlide", Q_ARG(QVariant, QVariant(2)));
      settle();
      auto* download = openDownload();
      if (!download || !download->property("current").toBool() || !settleOrientation(orientation)) return 1;
      auto* button = download->findChild<QQuickItem*>("startDownloadButton");
      if (!button) return 1;
      validationCore->setProperty("ready", false);
      const int count = validationCore->property("requestCount").toInt();
      QMetaObject::invokeMethod(download, "start");
      if (download->property("busy").toBool() || validationCore->property("requestCount").toInt() != count) return 1;
      validationCore->setProperty("ready", true);
      if (!QMetaObject::invokeMethod(download, "start")) return 1;
      int id = download->property("pending").toInt();
      const auto request = QJsonDocument::fromJson(
          validationCore->property("lastRequest").toString().toUtf8()).toVariant().toMap();
      if (!id || !download->property("busy").toBool() || button->property("enabled").toBool() ||
          request.value("method") != "downloadEnqueue" ||
          !request.value("args").toMap().value("current").toBool() ||
          request.value("args").toMap().value("quality") != "low") {
        qCritical("SILICA_DOWNLOAD_START_FAIL id=%d busy=%d buttonEnabled=%d request=%s", id,
            download->property("busy").toBool(), button->property("enabled").toBool(),
            qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(request).toJson(QJsonDocument::Compact))));
        return 1;
      }
      const int afterStart = validationCore->property("requestCount").toInt();
      QMetaObject::invokeMethod(download, "start");
      if (validationCore->property("requestCount").toInt() != afterStart) return 1;
      if (orientation == 2) {
        const QString failure = QStringLiteral("隔离测试下载失败");
        if (!completeDownload(id, failure) || download->property("busy").toBool() ||
            download->property("enqueued").toBool() || download->property("errorText") != failure ||
            currentPage() != download || !button->property("enabled").toBool()) return 1;
        if (!QMetaObject::invokeMethod(download, "start")) return 1;
        id = download->property("pending").toInt();
        if (!id || validationCore->property("requestCount").toInt() != afterStart + 1) return 1;
      }
      const int beforeComplete = validationCore->property("requestCount").toInt();
      if (!completeDownload(id, QString()) || !download->property("enqueued").toBool() || button->property("enabled").toBool()) return 1;
      QMetaObject::invokeMethod(download, "start");
      if (validationCore->property("requestCount").toInt() != beforeComplete || !waitForDownloads()) return 1;
      fprintf(stderr, "SILICA_DOWNLOAD_NAVIGATION orientation=%d menu-start-completion shell-depth=1 filter=0\n", orientation);
    }
    QMetaObject::invokeMethod(root, "switchTab", Q_ARG(QVariant, QVariant(0)));
    settle();
    auto* waitingDownload = openDownload();
    if (!waitingDownload) return 1;
    QMetaObject::invokeMethod(waitingDownload, "start");
    const int waitingId = waitingDownload->property("pending").toInt();
    QMetaObject::invokeMethod(root, "openQueue");
    if (!QQmlProperty(root, "pageStack.busy").read().toBool() || !completeDownload(waitingId, QString())) return 1;
    settle();
    if (currentPage()->objectName() != "queuePage" || routeState().value("depth").toInt() != 4) return 1;
    QMetaObject::invokeMethod(root, "openUnique", Q_ARG(QVariant, QVariant("DownloadSongPage.qml")),
        Q_ARG(QVariant, QVariant("downloadSongPage")), Q_ARG(QVariant, QVariant(QVariantMap())));
    settle();
    if (!waitForDownloads()) return 1;
    QMetaObject::invokeMethod(root, "switchTab", Q_ARG(QVariant, QVariant(0)));
    settle();
    auto* abandonedDownload = openDownload();
    if (!abandonedDownload) return 1;
    QMetaObject::invokeMethod(abandonedDownload, "start");
    const int abandonedId = abandonedDownload->property("pending").toInt();
    QMetaObject::invokeMethod(root, "switchTab", Q_ARG(QVariant, QVariant(0)));
    settle();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const QString previousError = root->property("errorText").toString();
    if (!completeDownload(abandonedId, "isolated-late-error") ||
        root->property("errorText").toString() != previousError ||
        routeState().value("page").toString() != "exploreRoot" ||
        routeState().value("depth").toInt() != 1) return 1;
    fprintf(stderr, "SILICA_DOWNLOAD_NAVIGATION_PASS player-menu start duplicate-guard error-retry transition deferred-return abandoned-callback shell-all-filter\n");
    QMetaObject::invokeMethod(root, "switchTab", Q_ARG(QVariant, QVariant(2)));
    settle();
    auto* library = QQmlProperty(root, "homePage.currentPane").read().value<QQuickItem*>();
    auto* settingsEntry = library ? library->findChild<QQuickItem*>("librarySettingsEntry") : nullptr;
    auto* aboutEntry = library ? library->findChild<QQuickItem*>("libraryAboutEntry") : nullptr;
    validationCore->setProperty("ready", false);
    const int beforeAbout = validationCore->property("requestCount").toInt();
    QObject* clickEvent = nullptr;
    if (!settingsEntry || !aboutEntry || settingsEntry->parentItem() != aboutEntry->parentItem() ||
        aboutEntry->y() < settingsEntry->y() + settingsEntry->height() - 1 ||
        !aboutEntry->property("enabled").toBool() ||
        !QMetaObject::invokeMethod(aboutEntry, "clicked", QGenericArgument("QQuickMouseEvent*", &clickEvent))) {
      qCritical("SILICA_ABOUT_NAVIGATION_FAIL library-entry");
      return 1;
    }
    settle();
    if (currentPage()->objectName() != "aboutPage" || routeState().value("depth").toInt() != 2) {
      qCritical("SILICA_ABOUT_NAVIGATION_FAIL opened-page state=%s",
          qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(routeState()).toJson(QJsonDocument::Compact))));
      return 1;
    }
    auto* aboutFooter = currentPage()->findChild<QQuickItem*>("appFooter");
    auto* aboutMini = aboutFooter ? aboutFooter->findChild<QQuickItem*>("footerMiniPlayer") : nullptr;
    if (!aboutMini) {
      qCritical("SILICA_ABOUT_NAVIGATION_FAIL missing-footer page=%s footer=%p",
          qPrintable(currentPage()->objectName()), static_cast<void*>(aboutFooter));
      return 1;
    }
    const bool originalMiniVisible = aboutMini->isVisible();
    for (int orientation : {1, 2, 4, 8, 1}) {
      root->setProperty("deviceOrientation", orientation);
      if (!settleOrientation(orientation)) {
        qCritical("SILICA_ABOUT_NAVIGATION_FAIL orientation expected=%d page=%d window=%d allowed=%d size=%gx%g",
            orientation, currentPage()->property("orientation").toInt(),
            currentPage()->property("_windowOrientation").toInt(),
            currentPage()->property("allowedOrientations").toInt(),
            currentPage()->width(), currentPage()->height());
        return 1;
      }
      for (bool visible : {false, true}) {
        aboutMini->setVisible(visible);
        if (!ValidateAboutContent(app, *validationLayout, currentPage())) return 1;
      }
    }
    aboutMini->setVisible(originalMiniVisible);
    auto* pageStack = QQmlProperty(root, "pageStack").read().value<QObject*>();
    if (!pageStack || !QMetaObject::invokeMethod(pageStack, "pop",
        Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()))) return 1;
    settle();
    if (routeState().value("page").toString() != "libraryRoot" ||
        routeState().value("depth").toInt() != 1 ||
        validationCore->property("requestCount").toInt() != beforeAbout) return 1;
    validationCore->setProperty("ready", true);
    fprintf(stderr, "SILICA_ABOUT_CONTENT_PASS icon name version developer two-projects four-paragraph-disclaimer responsive-scroll four-orientations\n");
    fprintf(stderr, "SILICA_ABOUT_NAVIGATION_PASS below-settings offline entry-click page-stack back footer\n");
    if (!validateDownloadDirectory()) return 1;
    root->setProperty("allowedOrientations", originalAllowed);
    root->setProperty("deviceOrientation", originalDevice);
    fprintf(stderr,"SILICA_APPLICATION_ROTATION_PASS four-orientations normal-page-stack four-tabs player queue portrait-return\n");
    fprintf(stderr,"SILICA_PLAYER_OVERFLOW_PASS scrollable-controls long-errors\n");
    fprintf(stderr,"SILICA_SEARCH_LAYOUT_PASS four-orientations mini-player empty-query empty-results both-types busy-error\n");
    fprintf(stderr,"SILICA_PLAYER_MENU_PASS page-top ownership full-width header-pointer-drag cover-lyrics\n");
    fprintf(stderr,"SILICA_VALIDATION_ISOLATED_CORE_PASS requests-use-fixture\n");
    for (int cycle = 0; cycle < 2; ++cycle) {
      QPointer<QObject> oldRoot(view.rootObject());
      QCloseEvent close;
      QCoreApplication::sendEvent(&view, &close);
      app.processEvents();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      if (oldRoot || view.rootObject() || control.requested || !view.source().isEmpty()) return 1;
      control.Show();
      view.hide();
      if (!view.rootObject() || view.status() == QQuickView::Error ||
          view.rootContext()->contextProperty("core").value<QObject*>() != validationCore.get()) return 1;
      view.rootObject()->setParentItem(validationWindow->contentItem());
      app.processEvents();
    }
    fprintf(stderr,"SILICA_PRESENTATION_REOPEN_PASS close-event qml-release same-core two-cycles\n");
    fprintf(stderr,app.arguments().contains("--shell-only")?"SILICA_SHELL_ONLY_VALIDATION_PASS\n":"SILICA_QML_VALIDATION_PASS components=32\n");
    return 0;
  }
  NativeMpris mpris(runtime->player(), *bridge.artworkBackend(),
      [&runtime]() { return runtime->artworkSource(); }, [&control]() { control.Show(); });
  if (!mpris.start()) { qCritical("MCONNECT_NATIVE_MPRIS_START_FAILED"); return 1; }
  QObject::connect(&control, &NativePresentation::presentationClosed, &bridge, &CoreBridge::suspend);
  control.keepAlive = [&]() {
    return provider.sessionCount() > 0 || runtime->hasOngoingWork() || bridge.hasPendingWork();
  };
  const auto publish = [&]() {
    auto media = runtime->player().snapshot();
    // D-Bus variants require a concrete type, including before a song is loaded.
    media["song"] = media.value("song").toMap();
    provider.publish({{"media", media}});
  };
  QObject::connect(&runtime->player(), &NativePlayer::changed, &provider, publish);
  publish();
  QObject::connect(&control, &NativePresentation::idle, &app, [&]() {
    qInfo("MCONNECT_NATIVE_IDLE_EXIT");
    app.quit();
  });
  control.monitor();
  if (!background || control.requested) control.Show();
  else qInfo("MCONNECT_NATIVE_BACKGROUND_READY");
  const int result = app.exec();
  provider.close();
  mpris.close();
  runtime->close();
  bus.unregisterObject("/org/sailfishos/Mconnect/Silica");
  bus.unregisterObject("/org/sailfishos/Mconnect/Artwork");
  bus.unregisterObject("/org/sailfishos/Mconnect/Platforms");
  bus.unregisterService("org.sailfishos.Mconnect.Silica");
  activation.close();
  return result;
}
