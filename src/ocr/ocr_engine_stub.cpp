// Systems with no built-in engine Patchy can use yet (macOS, Linux, the web build): the
// list is empty and the UI says so (docs/ocr.md).

#include "ocr/ocr_platform.hpp"

namespace patchy::ocr::detail {

std::vector<std::shared_ptr<Engine>> platform_engines() {
  return {};
}

}  // namespace patchy::ocr::detail
