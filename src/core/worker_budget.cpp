#include "core/worker_budget.hpp"

#include <algorithm>

#if defined(__EMSCRIPTEN_PTHREADS__)
#include <emscripten.h>
#include <emscripten/threading.h>
#endif

namespace patchy {

int max_blocking_fanout_workers(int wanted) {
#if defined(__EMSCRIPTEN_PTHREADS__)
  if (emscripten_is_main_browser_thread()) {
    // PThread.unusedWorkers holds the pre-spawned pool workers not currently
    // running a pthread. Two workers of margin absorb spawns racing in from
    // worker threads between this read and the fan-out's own pthread_create
    // calls (main-thread spawns cannot interleave; this thread does not
    // yield between the read and the creates).
    const int idle = EM_ASM_INT({
      return (typeof PThread === 'undefined' || !PThread.unusedWorkers) ? 0 : PThread.unusedWorkers.length;
    });
    return std::clamp(idle - 2, 0, wanted);
  }
#endif
  return wanted;
}

}  // namespace patchy
