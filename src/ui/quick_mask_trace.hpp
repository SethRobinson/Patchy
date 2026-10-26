#pragma once

// PATCHY_QUICK_MASK_TRACE=1 prints every Quick Mask change with its source, and the
// keyboard events of its shortcut, on stderr (GitHub issue 437: one tap of Q turned
// Quick Mask on and straight off on a Linux desktop with ibus).

#include <QElapsedTimer>
#include <QString>
#include <QtGlobal>

#include <cstdio>

namespace patchy::ui {

inline bool quick_mask_trace_enabled() {
  static const bool enabled = qEnvironmentVariableIntValue("PATCHY_QUICK_MASK_TRACE") == 1;
  return enabled;
}

inline void quick_mask_trace(const QString& message) {
  if (!quick_mask_trace_enabled()) {
    return;
  }
  static QElapsedTimer clock;
  if (!clock.isValid()) {
    clock.start();
  }
  std::fprintf(stderr, "[quick-mask %7lld ms] %s\n", static_cast<long long>(clock.elapsed()),
               message.toUtf8().constData());
  std::fflush(stderr);
}

}  // namespace patchy::ui
