#include "ui/cli_exit.hpp"

#include <QCoreApplication>

#ifdef Q_OS_WASM
#include <emscripten.h>
#endif

namespace patchy::ui {

void exit_cli_application(int code) {
#ifdef Q_OS_WASM
  // QCoreApplication::exit leaves a parked tab here (see the header); shut the
  // Emscripten runtime down instead so the page learns the exit code.
  emscripten_force_exit(code);
#else
  QCoreApplication::exit(code);
#endif
}

}  // namespace patchy::ui
