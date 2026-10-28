#include "ui/theme_apply_trace.hpp"

#include <QAbstractEventDispatcher>
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QObject>
#include <QPointer>
#include <QStyle>
#include <QStyleHints>

#include <array>
#include <cstdio>
#include <memory>
#include <optional>

namespace patchy::ui {

namespace {

// The restyle events worth counting: each one a widget receives is work Qt does for it.
constexpr std::array<QEvent::Type, 6> kCountedEvents{QEvent::Polish,      QEvent::StyleChange, QEvent::PaletteChange,
                                                     QEvent::ThemeChange, QEvent::FontChange,  QEvent::Paint};

class EventCounter final : public QObject {
 public:
  std::array<int, kCountedEvents.size()> counts{};

  bool eventFilter(QObject* watched, QEvent* event) override {
    for (std::size_t i = 0; i < kCountedEvents.size(); ++i) {
      if (event->type() == kCountedEvents[i]) {
        ++counts[i];
      }
    }
    return QObject::eventFilter(watched, event);
  }
};

const char* event_name(QEvent::Type type) {
  switch (type) {
    case QEvent::Polish:
      return "Polish";
    case QEvent::StyleChange:
      return "StyleChange";
    case QEvent::PaletteChange:
      return "PaletteChange";
    case QEvent::ThemeChange:
      return "ThemeChange";
    case QEvent::FontChange:
      return "FontChange";
    case QEvent::Paint:
      return "Paint";
    default:
      return "?";
  }
}

struct TraceState {
  int depth = 0;
  int applies = 0;
  std::optional<bool> enabled_override;
  const char* what = "";
  QElapsedTimer clock;
  double last_ms = 0.0;
  std::vector<ThemeApplyPhase> phases;
  std::vector<ThemeApplyPhase> last_phases;
  QPointer<EventCounter> counter;
};

TraceState& state() {
  static TraceState trace;
  return trace;
}

void print_counts(const EventCounter& counter, const char* label) {
  std::fprintf(stderr, "  %s:", label);
  for (std::size_t i = 0; i < kCountedEvents.size(); ++i) {
    std::fprintf(stderr, " %s %d", event_name(kCountedEvents[i]), counter.counts[i]);
  }
  std::fprintf(stderr, "\n");
}

}  // namespace

bool theme_apply_trace_enabled() {
  auto& trace = state();
  if (trace.enabled_override) {
    return *trace.enabled_override;
  }
  static const bool from_environment = qEnvironmentVariableIntValue("PATCHY_THEME_TRACE") != 0;
  return from_environment;
}

void set_theme_apply_trace_enabled_for_testing(bool enabled) { state().enabled_override = enabled; }

ThemeApplyScope::ThemeApplyScope(const char* what) {
  auto& trace = state();
  if (trace.depth++ > 0) {
    return;
  }
  ++trace.applies;
  // The wait cursor goes up before any work. Windows and X11 show it at once; a
  // Wayland client only sends it when its queue is flushed, which the event
  // dispatcher does, so let it run once (no input: a click must not start another
  // apply inside this one). Nothing has changed yet, so a timer that applies a
  // theme in there still sees a consistent state.
  if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr) {
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
      --trace.depth;
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      ++trace.depth;
    }
  }
  if (!theme_apply_trace_enabled()) {
    return;
  }
  trace.what = what;
  trace.phases.clear();
  trace.clock.start();
  trace.last_ms = 0.0;
  if (trace.counter == nullptr && QCoreApplication::instance() != nullptr) {
    trace.counter = new EventCounter;
    QCoreApplication::instance()->installEventFilter(trace.counter);
  }
}

ThemeApplyScope::~ThemeApplyScope() {
  auto& trace = state();
  if (--trace.depth > 0) {
    return;
  }
  if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr) {
    QGuiApplication::restoreOverrideCursor();
  }
  if (!trace.clock.isValid()) {
    return;
  }
  theme_apply_phase("rest");
  trace.clock.invalidate();
  const double total = trace.last_ms;
  const auto* style = QApplication::style();
  std::fprintf(stderr, "[theme-trace] %s: %.1f ms (platform %s, style %s, Qt scheme %d)\n", trace.what, total,
               qPrintable(QGuiApplication::platformName()), style != nullptr ? qPrintable(style->name()) : "none",
               static_cast<int>(QGuiApplication::styleHints()->colorScheme()));
  for (const auto& phase : trace.phases) {
    std::fprintf(stderr, "  %-28s %8.1f ms\n", qPrintable(phase.name), phase.ms);
  }
  if (trace.counter != nullptr) {
    print_counts(*trace.counter, "events during apply");
  }
  std::fflush(stderr);
  trace.last_phases = trace.phases;
  // Queued platform theme changes and the repaint land after the apply returns:
  // time the event loop's way back to idle and count what it delivered.
  const auto applies = trace.applies;
  QElapsedTimer settle;
  settle.start();
  if (trace.counter != nullptr) {
    trace.counter->counts = {};
  }
  auto* dispatcher = QAbstractEventDispatcher::instance();
  if (dispatcher == nullptr) {
    return;
  }
  // The first time the loop is about to sleep, every queued event and repaint is done.
  auto connection = std::make_shared<QMetaObject::Connection>();
  *connection = QObject::connect(dispatcher, &QAbstractEventDispatcher::aboutToBlock, [applies, settle, connection] {
    QObject::disconnect(*connection);
    auto& later = state();
    std::fprintf(stderr, "[theme-trace] back to idle after %.1f ms\n",
                 static_cast<double>(settle.nsecsElapsed()) / 1.0e6);
    if (later.counter != nullptr) {
      print_counts(*later.counter, "events after apply");
      if (later.applies == applies && later.depth == 0) {
        delete later.counter;
      }
    }
    std::fflush(stderr);
  });
}

void theme_apply_phase(const char* name) {
  auto& trace = state();
  if (trace.depth == 0 || !trace.clock.isValid()) {
    return;
  }
  const double now = static_cast<double>(trace.clock.nsecsElapsed()) / 1.0e6;
  trace.phases.push_back({QString::fromLatin1(name), now - trace.last_ms});
  trace.last_ms = now;
}

std::vector<ThemeApplyPhase> last_theme_apply_phases() { return state().last_phases; }

}  // namespace patchy::ui
