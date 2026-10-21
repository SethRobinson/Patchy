#include "ui/portal_color_pick.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QPointer>

#include <algorithm>
#include <utility>

namespace patchy::ui {

namespace {

constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kScreenshotInterface = "org.freedesktop.portal.Screenshot";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";

}  // namespace

PortalColorPickRequest::PortalColorPickRequest(QObject* context, std::function<void(std::optional<QColor>)> done)
    : QObject(context), done_(std::move(done)) {}

void PortalColorPickRequest::finish(std::optional<QColor> color) {
  if (finished_) {
    return;
  }
  finished_ = true;
  auto done = std::move(done_);
  deleteLater();
  if (done) {
    done(color);
  }
}

void PortalColorPickRequest::response(uint code, const QVariantMap& results) {
  if (code != 0) {
    finish(std::nullopt);
    return;
  }
  const auto color_value = results.value(QStringLiteral("color"));
  if (!color_value.canConvert<QDBusArgument>()) {
    finish(std::nullopt);
    return;
  }
  // Const: on a non-const QDBusArgument beginStructure() is the marshalling
  // overload, which does nothing on this received (read-only) argument. The
  // first read then landed on the struct itself and libdbus aborted the
  // process (GitHub issue 128 follow-up). Anything but (ddd) is reported as no
  // color: reading past a shorter struct is just as fatal.
  const auto argument = color_value.value<QDBusArgument>();
  if (argument.currentSignature() != QLatin1String("(ddd)")) {
    finish(std::nullopt);
    return;
  }
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
  argument.beginStructure();
  argument >> red >> green >> blue;
  argument.endStructure();
  finish(QColor::fromRgbF(static_cast<float>(std::clamp(red, 0.0, 1.0)),
                          static_cast<float>(std::clamp(green, 0.0, 1.0)),
                          static_cast<float>(std::clamp(blue, 0.0, 1.0))));
}

bool portal_color_pick_preferred() {
  return QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive);
}

QString portal_request_path(const QString& unique_bus_name, const QString& handle_token) {
  auto sender = unique_bus_name;
  if (sender.startsWith(QLatin1Char(':'))) {
    sender.remove(0, 1);
  }
  sender.replace(QLatin1Char('.'), QLatin1Char('_'));
  return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, handle_token);
}

bool request_portal_color_pick(QObject* context, std::function<void(std::optional<QColor>)> done) {
  auto bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    return false;
  }
  static int request_counter = 0;
  const auto token = QStringLiteral("patchy_pick_%1").arg(++request_counter);
  auto* request = new PortalColorPickRequest(context, std::move(done));
  // Subscribe to the Response first: the portal may answer before the method
  // reply arrives, and a late subscription misses it.
  const auto request_path = portal_request_path(bus.baseService(), token);
  if (!bus.connect(QString::fromLatin1(kPortalService), request_path, QString::fromLatin1(kRequestInterface),
                   QStringLiteral("Response"), request, SLOT(response(uint, QVariantMap)))) {
    request->finish(std::nullopt);
    return false;
  }
  auto call = QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService), QString::fromLatin1(kPortalPath),
                                             QString::fromLatin1(kScreenshotInterface), QStringLiteral("PickColor"));
  QVariantMap options;
  options.insert(QStringLiteral("handle_token"), token);
  // No parent window handle: exporting one needs xdg_foreign, and the
  // compositor's picker is screen-wide anyway.
  call << QString() << options;
  auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), request);
  QObject::connect(watcher, &QDBusPendingCallWatcher::finished, request,
                   [request = QPointer<PortalColorPickRequest>(request)](QDBusPendingCallWatcher* finished) {
                     finished->deleteLater();
                     const QDBusPendingReply<QDBusObjectPath> reply(*finished);
                     if (reply.isError() && request != nullptr) {
                       // No portal (a bare session without xdg-desktop-portal):
                       // nothing will ever answer.
                       request->finish(std::nullopt);
                     }
                   });
  return true;
}

}  // namespace patchy::ui
