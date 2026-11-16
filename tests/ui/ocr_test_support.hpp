#pragma once

// Text recognition in the UI suite (docs/ocr.md, "Testing"). tests/ui/main.cpp installs an
// empty engine list at startup, so no test depends on which OCR languages this machine's
// Windows has. A test that needs an engine installs one under one of these guards; both
// put the empty list back when they go out of scope.

#include "ocr/ocr_engine.hpp"
#include "ocr/ocr_fake_engine.hpp"

#include "test_harness.hpp"

#include <memory>
#include <utility>
#include <vector>

// The text recognition test groups. ocr_ui_tests() (ocr_ui_tests.cpp, the one name the
// suite's group list knows) gathers them, so a new OCR test file is declared here and does
// not touch the suite-wide header every UI test file includes.
std::vector<patchy::test::TestCase> ocr_layers_ui_tests();
std::vector<patchy::test::TestCase> ocr_command_ui_tests();
std::vector<patchy::test::TestCase> pdf_import_ocr_ui_tests();
std::vector<patchy::test::TestCase> ocr_model_engine_ui_tests();
std::vector<patchy::test::TestCase> ocr_download_ui_tests();
std::vector<patchy::test::TestCase> pdf_searchable_text_ui_tests();
std::vector<patchy::test::TestCase> pdf_viewer_ocr_ui_tests();

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
