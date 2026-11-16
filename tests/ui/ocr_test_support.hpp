#pragma once

// Text recognition in the UI suite (docs/ocr.md, "Testing"). tests/ui/main.cpp installs an
// empty engine list at startup, so no test depends on which OCR languages this machine's
// Windows has. A test that needs an engine installs one under one of these guards; both
// put the empty list back when they go out of scope.

#include "ocr/ocr_engine.hpp"
#include "ocr/ocr_fake_engine.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace patchy::test::ui {

inline void install_no_ocr_engines() {
  patchy::ocr::set_engines_for_testing([] { return std::vector<std::shared_ptr<patchy::ocr::Engine>>{}; });
}

// The given engines (usually one fake) are the whole list for the scope.
class ScopedOcrEngines {
public:
  explicit ScopedOcrEngines(std::vector<std::shared_ptr<patchy::ocr::Engine>> engines) {
    patchy::ocr::set_engines_for_testing([engines = std::move(engines)] { return engines; });
  }
  ~ScopedOcrEngines() { install_no_ocr_engines(); }
  ScopedOcrEngines(const ScopedOcrEngines&) = delete;
  ScopedOcrEngines& operator=(const ScopedOcrEngines&) = delete;
};

// The machine's real engines for the scope: only for the "_if_available" tests that
// measure a real engine.
class ScopedRealOcrEngines {
public:
  ScopedRealOcrEngines() { patchy::ocr::set_engines_for_testing({}); }
  ~ScopedRealOcrEngines() { install_no_ocr_engines(); }
  ScopedRealOcrEngines(const ScopedRealOcrEngines&) = delete;
  ScopedRealOcrEngines& operator=(const ScopedRealOcrEngines&) = delete;
};

}  // namespace patchy::test::ui
