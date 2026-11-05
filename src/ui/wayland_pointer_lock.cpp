#include "ui/wayland_pointer_lock.hpp"

#include <QAbstractEventDispatcher>
#include <QByteArray>
#include <QGuiApplication>
#include <QMargins>
#include <QMetaObject>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformwindow.h>

#include <dlfcn.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

// The protocol tables below follow the published XML of wayland-protocols (MIT licence):
// unstable/pointer-constraints/pointer-constraints-unstable-v1.xml and
// unstable/relative-pointer/relative-pointer-unstable-v1.xml, written out by hand in the
// layout libwayland's wayland-util.h fixes (struct wl_message, struct wl_interface), so
// the build needs no wayland headers, scanner or library. Every libwayland call goes
// through symbols looked up in the copy Qt's Wayland plug-in loaded.

namespace patchy::ui {
#if QT_CONFIG(wayland)
namespace {

struct WlInterface;

struct WlMessage {
  const char* name;
  const char* signature;
  const WlInterface** types;
};

struct WlInterface {
  const char* name;
  int version;
  int method_count;
  const WlMessage* methods;
  int event_count;
  const WlMessage* events;
};

using WlFixed = std::int32_t;

WlFixed fixed_from_double(double value) { return static_cast<WlFixed>(std::lround(value * 256.0)); }
double fixed_to_double(WlFixed value) { return static_cast<double>(value) / 256.0; }

// Request opcodes and enum values from the protocol XML.
constexpr std::uint32_t kDisplayGetRegistry = 1;
constexpr std::uint32_t kRegistryBind = 0;
constexpr std::uint32_t kDestroy = 0;  // every interface used here has destroy as request 0
constexpr std::uint32_t kConstraintsLockPointer = 1;
constexpr std::uint32_t kLockedSetCursorPositionHint = 1;
constexpr std::uint32_t kRelativeManagerGetRelativePointer = 1;
constexpr std::uint32_t kLifetimeOneshot = 1;

// Resolved at load time: the core interfaces live in libwayland-client.
const WlInterface* g_lock_pointer_types[5] = {};
const WlInterface* g_get_relative_pointer_types[2] = {};
const WlInterface* g_set_region_types[1] = {};
const WlInterface* g_null_types[6] = {};

const WlMessage kConstraintsRequests[] = {
    {"destroy", "", g_null_types},
    {"lock_pointer", "noo?ou", g_lock_pointer_types},
    {"confine_pointer", "noo?ou", g_null_types},
};
const WlInterface kPointerConstraintsInterface = {"zwp_pointer_constraints_v1", 1, 3, kConstraintsRequests, 0,
                                                  nullptr};

const WlMessage kLockedRequests[] = {
    {"destroy", "", g_null_types},
    {"set_cursor_position_hint", "ff", g_null_types},
    {"set_region", "?o", g_set_region_types},
};
const WlMessage kLockedEvents[] = {
    {"locked", "", g_null_types},
    {"unlocked", "", g_null_types},
};
const WlInterface kLockedPointerInterface = {"zwp_locked_pointer_v1", 1, 3, kLockedRequests, 2, kLockedEvents};

const WlMessage kRelativeManagerRequests[] = {
    {"destroy", "", g_null_types},
    {"get_relative_pointer", "no", g_get_relative_pointer_types},
};
const WlInterface kRelativePointerManagerInterface = {"zwp_relative_pointer_manager_v1", 1, 2,
                                                      kRelativeManagerRequests, 0, nullptr};

const WlMessage kRelativeRequests[] = {
    {"destroy", "", g_null_types},
};
const WlMessage kRelativeEvents[] = {
    {"relative_motion", "uuffff", g_null_types},
};
const WlInterface kRelativePointerInterface = {"zwp_relative_pointer_v1", 1, 1, kRelativeRequests, 1,
                                               kRelativeEvents};

using Listener = void (**)(void);

struct Api {
  void* (*marshal_constructor)(void* proxy, std::uint32_t opcode, const WlInterface* interface, ...) = nullptr;
  void* (*marshal_constructor_versioned)(void* proxy, std::uint32_t opcode, const WlInterface* interface,
                                         std::uint32_t version, ...) = nullptr;
  void (*marshal)(void* proxy, std::uint32_t opcode, ...) = nullptr;
  int (*add_listener)(void* proxy, Listener implementation, void* data) = nullptr;
  void (*proxy_destroy)(void* proxy) = nullptr;
  void* (*create_wrapper)(void* proxy) = nullptr;
  void (*wrapper_destroy)(void* proxy_wrapper) = nullptr;
  void (*set_queue)(void* proxy, void* queue) = nullptr;
  void* (*create_queue)(void* display) = nullptr;
  void (*queue_destroy)(void* queue) = nullptr;
  int (*roundtrip_queue)(void* display, void* queue) = nullptr;
  int (*dispatch_queue_pending)(void* display, void* queue) = nullptr;
  int (*flush)(void* display) = nullptr;
  const WlInterface* registry_interface = nullptr;
  const WlInterface* surface_interface = nullptr;
  const WlInterface* pointer_interface = nullptr;
  const WlInterface* region_interface = nullptr;

  bool load() {
    // RTLD_NOLOAD: only the copy Qt's Wayland plug-in already uses. An X11 session has
    // none loaded, and the app never pulls in a library the system lacks.
    void* library = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_NOLOAD);
    if (library == nullptr) {
      return false;
    }
    const auto resolve = [library](auto& target, const char* name) {
      void* symbol = dlsym(library, name);
      std::memcpy(&target, &symbol, sizeof(symbol));
      return symbol != nullptr;
    };
    return resolve(marshal_constructor, "wl_proxy_marshal_constructor") &&
           resolve(marshal_constructor_versioned, "wl_proxy_marshal_constructor_versioned") &&
           resolve(marshal, "wl_proxy_marshal") && resolve(add_listener, "wl_proxy_add_listener") &&
           resolve(proxy_destroy, "wl_proxy_destroy") && resolve(create_wrapper, "wl_proxy_create_wrapper") &&
           resolve(wrapper_destroy, "wl_proxy_wrapper_destroy") && resolve(set_queue, "wl_proxy_set_queue") &&
           resolve(create_queue, "wl_display_create_queue") && resolve(queue_destroy, "wl_event_queue_destroy") &&
           resolve(roundtrip_queue, "wl_display_roundtrip_queue") &&
           resolve(dispatch_queue_pending, "wl_display_dispatch_queue_pending") &&
           resolve(flush, "wl_display_flush") && resolve(registry_interface, "wl_registry_interface") &&
           resolve(surface_interface, "wl_surface_interface") && resolve(pointer_interface, "wl_pointer_interface") &&
           resolve(region_interface, "wl_region_interface");
  }
};

// The two protocol globals, bound once per process on a private queue that only this module
// dispatches (Qt's event thread reads the socket and files the events into it). Never freed:
// the proxies would outlive Qt's display at exit otherwise.
struct Globals {
  Api api;
  void* display = nullptr;
  void* queue = nullptr;
  void* constraints = nullptr;
  void* relative_manager = nullptr;

  static void on_global(void* data, void* registry, std::uint32_t name, const char* interface,
                        std::uint32_t version) {
    auto* self = static_cast<Globals*>(data);
    static_cast<void>(version);  // both protocols are at version 1
    if (std::strcmp(interface, kPointerConstraintsInterface.name) == 0 && self->constraints == nullptr) {
      self->constraints = self->api.marshal_constructor_versioned(
          registry, kRegistryBind, &kPointerConstraintsInterface, 1U, name, kPointerConstraintsInterface.name, 1U,
          static_cast<void*>(nullptr));
    } else if (std::strcmp(interface, kRelativePointerManagerInterface.name) == 0 &&
               self->relative_manager == nullptr) {
      self->relative_manager = self->api.marshal_constructor_versioned(
          registry, kRegistryBind, &kRelativePointerManagerInterface, 1U, name,
          kRelativePointerManagerInterface.name, 1U, static_cast<void*>(nullptr));
    }
  }
  static void on_global_remove(void*, void*, std::uint32_t) {}

  bool init(void* wl_display) {
    if (!api.load()) {
      return false;
    }
    g_lock_pointer_types[0] = &kLockedPointerInterface;
    g_lock_pointer_types[1] = api.surface_interface;
    g_lock_pointer_types[2] = api.pointer_interface;
    g_lock_pointer_types[3] = api.region_interface;
    g_get_relative_pointer_types[0] = &kRelativePointerInterface;
    g_get_relative_pointer_types[1] = api.pointer_interface;
    g_set_region_types[0] = api.region_interface;

    display = wl_display;
    queue = api.create_queue(display);
    if (queue == nullptr) {
      return false;
    }
    void* display_wrapper = api.create_wrapper(display);
    if (display_wrapper == nullptr) {
      return false;
    }
    api.set_queue(display_wrapper, queue);
    void* registry = api.marshal_constructor(display_wrapper, kDisplayGetRegistry, api.registry_interface,
                                             static_cast<void*>(nullptr));
    api.wrapper_destroy(display_wrapper);
    if (registry == nullptr) {
      return false;
    }
    static void (*const registry_listener[])(void) = {
        reinterpret_cast<void (*)(void)>(&Globals::on_global),
        reinterpret_cast<void (*)(void)>(&Globals::on_global_remove),
    };
    api.add_listener(registry, const_cast<Listener>(registry_listener), this);
    api.roundtrip_queue(display, queue);
    api.proxy_destroy(registry);  // the bound globals stay valid without it
    return constraints != nullptr && relative_manager != nullptr;
  }
};

Globals* wayland_globals(void* wl_display) {
  static Globals* globals = [wl_display]() -> Globals* {
    auto* candidate = new Globals;
    if (!candidate->init(wl_display)) {
      return nullptr;  // leaked on purpose with any half-bound proxy: see Globals
    }
    return candidate;
  }();
  if (globals != nullptr && globals->display != wl_display) {
    return nullptr;  // Qt reconnected to another display; never mix proxies across them
  }
  return globals;
}

class WaylandPointerLock final : public PointerLockSession {
 public:
  WaylandPointerLock(Globals& globals, PointerLockEvents events, double scale)
      : globals_(globals), events_(std::move(events)), scale_(scale > 0.0 ? scale : 1.0) {}

  ~WaylandPointerLock() override {
    QObject::disconnect(awake_connection_);
    if (relative_ != nullptr) {
      globals_.api.marshal(relative_, kDestroy);
      globals_.api.proxy_destroy(relative_);
    }
    if (locked_ != nullptr) {
      // Destroying the lock is what makes KWin and Mutter apply the cursor position hint.
      globals_.api.marshal(locked_, kDestroy);
      globals_.api.proxy_destroy(locked_);
    }
    globals_.api.flush(globals_.display);
  }

  WaylandPointerLock(const WaylandPointerLock&) = delete;
  WaylandPointerLock& operator=(const WaylandPointerLock&) = delete;

  bool start(void* surface, void* pointer, QPointF surface_hint) {
    auto& api = globals_.api;
    locked_ = api.marshal_constructor(globals_.constraints, kConstraintsLockPointer, &kLockedPointerInterface,
                                      static_cast<void*>(nullptr), surface, pointer, static_cast<void*>(nullptr),
                                      kLifetimeOneshot);
    relative_ = api.marshal_constructor(globals_.relative_manager, kRelativeManagerGetRelativePointer,
                                        &kRelativePointerInterface, static_cast<void*>(nullptr), pointer);
    if (locked_ == nullptr || relative_ == nullptr) {
      return false;
    }
    static void (*const locked_listener[])(void) = {
        reinterpret_cast<void (*)(void)>(&WaylandPointerLock::on_locked),
        reinterpret_cast<void (*)(void)>(&WaylandPointerLock::on_unlocked),
    };
    static void (*const relative_listener[])(void) = {
        reinterpret_cast<void (*)(void)>(&WaylandPointerLock::on_relative_motion),
    };
    api.add_listener(locked_, const_cast<Listener>(locked_listener), this);
    api.add_listener(relative_, const_cast<Listener>(relative_listener), this);
    // Double-buffered: it takes effect with the surface's next commit, which the drag's
    // first repaint makes.
    api.marshal(locked_, kLockedSetCursorPositionHint, fixed_from_double(surface_hint.x()),
                fixed_from_double(surface_hint.y()));
    api.flush(globals_.display);

    // Qt's event thread files our events into the private queue; dispatch them whenever the
    // GUI thread wakes, and on a short timer in case nothing else wakes it.
    if (auto* dispatcher = QAbstractEventDispatcher::instance(); dispatcher != nullptr) {
      awake_connection_ = QObject::connect(dispatcher, &QAbstractEventDispatcher::awake, &timer_,
                                           [this]() { dispatch(); });
    }
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(4);
    QObject::connect(&timer_, &QTimer::timeout, &timer_, [this]() { dispatch(); });
    timer_.start();
    return true;
  }

 private:
  void dispatch() {
    if (!dispatching_) {
      dispatching_ = true;
      globals_.api.dispatch_queue_pending(globals_.display, globals_.queue);
      dispatching_ = false;
    }
  }

  static void on_locked(void* data, void*) {
    auto* self = static_cast<WaylandPointerLock*>(data);
    if (self->events_.locked) {
      self->events_.locked();
    }
  }
  static void on_unlocked(void* data, void*) {
    auto* self = static_cast<WaylandPointerLock*>(data);
    if (self->events_.unlocked) {
      self->events_.unlocked();
    }
  }
  static void on_relative_motion(void* data, void*, std::uint32_t, std::uint32_t, WlFixed dx, WlFixed dy, WlFixed,
                                 WlFixed) {
    auto* self = static_cast<WaylandPointerLock*>(data);
    if (self->events_.relative_motion) {
      self->events_.relative_motion(QPointF(fixed_to_double(dx), fixed_to_double(dy)) / self->scale_);
    }
  }

  Globals& globals_;
  PointerLockEvents events_;
  double scale_ = 1.0;  // surface pixels per widget pixel (QT_SCALE_FACTOR)
  void* locked_ = nullptr;
  void* relative_ = nullptr;
  QTimer timer_;
  QMetaObject::Connection awake_connection_;
  bool dispatching_ = false;
};

}  // namespace

std::unique_ptr<PointerLockSession> create_wayland_pointer_lock(QWidget& widget, QPoint widget_position,
                                                                PointerLockEvents events) {
  if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
    return nullptr;
  }
  auto* application = qGuiApp != nullptr ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
  QWidget* top_level = widget.window();
  QWindow* window = top_level != nullptr ? top_level->windowHandle() : nullptr;
  auto* native = QGuiApplication::platformNativeInterface();
  if (application == nullptr || window == nullptr || window->handle() == nullptr || native == nullptr) {
    return nullptr;
  }
  void* display = application->display();
  void* pointer = application->pointer();
  void* surface = native->nativeResourceForWindow(QByteArrayLiteral("surface"), window);
  if (display == nullptr || pointer == nullptr || surface == nullptr) {
    return nullptr;
  }
  Globals* globals = wayland_globals(display);
  if (globals == nullptr) {
    return nullptr;
  }
  // Widget pixels are Qt's logical pixels; the surface's own coordinates differ from them only
  // by Qt's scale factor (QT_SCALE_FACTOR) and the client-side decoration margins.
  const double platform_ratio = window->handle()->devicePixelRatio();
  const double scale = platform_ratio > 0.0 ? window->devicePixelRatio() / platform_ratio : 1.0;
  const QMargins margins = window->frameMargins();
  const QPoint in_surface = widget.mapTo(top_level, widget_position) + QPoint(margins.left(), margins.top());
  const QPointF hint = QPointF(in_surface) * scale;
  auto lock = std::make_unique<WaylandPointerLock>(*globals, std::move(events), scale);
  if (!lock->start(surface, pointer, hint)) {
    return nullptr;
  }
  return lock;
}
#else
std::unique_ptr<PointerLockSession> create_wayland_pointer_lock(QWidget& widget, QPoint widget_position,
                                                                PointerLockEvents events) {
  static_cast<void>(widget);
  static_cast<void>(widget_position);
  static_cast<void>(events);
  return nullptr;
}
#endif

}  // namespace patchy::ui
