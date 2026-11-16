#pragma once

// The operating system's own engines: ocr_engine_win.cpp on Windows, ocr_engine_stub.cpp
// (an empty list) everywhere else. Internal to src/ocr; callers use engines().

#include "ocr/ocr_engine.hpp"

#include <memory>
#include <vector>

namespace patchy::ocr::detail {

[[nodiscard]] std::vector<std::shared_ptr<Engine>> platform_engines();

}  // namespace patchy::ocr::detail
