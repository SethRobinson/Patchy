// Crash handlers, bootstrap, and the runner loop moved verbatim from
// tests/ui_visual_tests.cpp. main() concatenates the group registration
// functions in the exact original registration-vector order.

#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include "test_fonts.hpp"
#include "test_harness.hpp"

#include "ui/app_settings.hpp"
#include "ui/localization.hpp"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QSettings>
#include <QString>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
#endif

using patchy::test::TestCase;
using patchy::test::visual_test_font;
using patchy::test::ui::cleanup_after_visual_test;
using patchy::test::ui::ensure_artifact_dir;

#ifdef Q_OS_WIN
// Print a symbolized stack when the suite hits an access violation. The
// process still dies (and WER still writes its dump), but the [PASS] log then
// ends with the faulting stack instead of stopping silently, which is the
// difference between a fixable report and an unreproduced flake.
LONG WINAPI report_access_violation(EXCEPTION_POINTERS* info) {
  if (info == nullptr || info->ExceptionRecord == nullptr ||
      info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const auto process = GetCurrentProcess();
  static bool symbols_ready = false;
  if (!symbols_ready) {
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    symbols_ready = SymInitialize(process, nullptr, TRUE) != FALSE;
  }
  fprintf(stderr, "[CRASH] access violation reading/writing %p at instruction %p\n",
          reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]),
          info->ExceptionRecord->ExceptionAddress);
  CONTEXT walk_context = *info->ContextRecord;
  STACKFRAME64 frame = {};
  frame.AddrPC.Offset = walk_context.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = walk_context.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = walk_context.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  for (int depth = 0; depth < 40; ++depth) {
    if (StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &walk_context, nullptr,
                    SymFunctionTableAccess64, SymGetModuleBase64, nullptr) == FALSE ||
        frame.AddrPC.Offset == 0) {
      break;
    }
    alignas(SYMBOL_INFO) char symbol_storage[sizeof(SYMBOL_INFO) + 512] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;
    DWORD64 symbol_displacement = 0;
    const char* name = SymFromAddr(process, frame.AddrPC.Offset, &symbol_displacement, symbol) != FALSE
                           ? symbol->Name
                           : "<unknown>";
    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(line);
    DWORD line_displacement = 0;
    if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_displacement, &line) != FALSE) {
      fprintf(stderr, "  #%02d %s (%s:%lu)\n", depth, name, line.FileName, line.LineNumber);
    } else {
      fprintf(stderr, "  #%02d %s +0x%llx\n", depth, name,
              static_cast<unsigned long long>(symbol_displacement));
    }
  }
  fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}
#else
// POSIX counterpart of report_access_violation above: on SIGSEGV/SIGBUS print the
// faulting address and a raw backtrace, then re-raise the default action so the process
// still dies with the correct signal status. backtrace_symbols_fd writes straight to
// the fd without allocating; frames print as module+offset (symbolize offline with
// atos/addr2line against the binary). fprintf is not strictly async-signal-safe, but a
// last-words crash reporter takes that trade -- same as the Windows handler.
extern "C" void report_fatal_signal(int signal_number, siginfo_t* info, void*) {
  fprintf(stderr, "[CRASH] fatal signal %d at address %p\n", signal_number,
          info != nullptr ? info->si_addr : nullptr);
  void* frames[64] = {};
  const int depth = backtrace(frames, 64);
  backtrace_symbols_fd(frames, depth, fileno(stderr));
  fflush(stderr);
  std::signal(signal_number, SIG_DFL);
  raise(signal_number);
}
#endif

int main(int argc, char* argv[]) {
  patchy::test::suppress_crash_dialogs();
#ifdef Q_OS_WIN
  AddVectoredExceptionHandler(1, report_access_violation);
#else
  struct sigaction crash_action {};
  crash_action.sa_sigaction = report_fatal_signal;
  crash_action.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &crash_action, nullptr);
  sigaction(SIGBUS, &crash_action, nullptr);
  // An uncaught exception calls terminate at the THROW site (before unwinding, per the
  // Itanium ABI), so the backtrace here points at the actual thrower.
  std::set_terminate([] {
    if (auto current = std::current_exception()) {
      try {
        std::rethrow_exception(current);
      } catch (const std::exception& error) {
        fprintf(stderr, "[CRASH] terminate: uncaught exception: %s\n", error.what());
      } catch (...) {
        fprintf(stderr, "[CRASH] terminate: uncaught non-std exception\n");
      }
    } else {
      fprintf(stderr, "[CRASH] terminate called without an active exception\n");
    }
    void* frames[64] = {};
    backtrace_symbols_fd(frames, backtrace(frames, 64), fileno(stderr));
    fflush(stderr);
    abort();
  });
#endif
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  QApplication app(argc, argv);
  app.setFont(visual_test_font());
  ensure_artifact_dir();
  const auto test_settings_path = QDir::current().filePath(QStringLiteral("test-artifacts/settings"));
  CHECK(QDir().mkpath(test_settings_path));
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, test_settings_path);
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools"));
    settings.remove(QStringLiteral("view"));
    settings.remove(QStringLiteral("input"));
    settings.remove(QStringLiteral("imports"));
    settings.remove(QStringLiteral("window"));
    settings.remove(QStringLiteral("filters/gallery"));
    settings.remove(QStringLiteral("preferences/language"));
    settings.setValue(QStringLiteral("updates/checkOnStartup"), false);
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false);

  std::vector<TestCase> tests;
  for (const auto& registration : {
           app_shell_tests,
           filter_catalog_dialog_tests,
           layer_style_gradient_tests,
           destructive_filters_gallery_tests,
           pickers_notices_hotkeys_tests,
           canvas_view_tools_tests,
           layer_context_lifecycle_tests,
           brush_pattern_palette_tests,
           layer_panel_organization_tests,
           move_tool_processing_overlay_tests,
           selection_marquee_lasso_tests,
           clipboard_free_transform_tests,
           channels_panel_tests,
           camera_raw_heif_tests,
           layer_mask_tests,
           pen_tablet_input_tests,
           brush_engine_stroke_tests,
           text_editor_font_picker_tests,
           psd_text_import_tests,
           text_transform_commit_tests,
           flat_image_format_tests,
           smart_filter_tests,
           smart_object_tests,
           warp_tests,
           import_print_resolution_tests,
           image_adjustments_curves_tests,
           selection_engines_tests,
           misc_visuals_outline_stress_tests,
           float_window_tests,
           readme_screenshot_tests,
       }) {
    auto group = registration();
    tests.insert(tests.end(), std::make_move_iterator(group.begin()),
                 std::make_move_iterator(group.end()));
  }

  std::string filter;
  const auto env_filter = qgetenv("PATCHY_UI_TEST_FILTER");
  if (!env_filter.isEmpty()) {
    filter = env_filter.toStdString();
  }
  if (argc > 1) {
    filter = argv[1];
  }

  int failures = 0;
  for (const auto& test : tests) {
    if (!filter.empty() && test.name.find(filter) == std::string::npos) {
      continue;
    }
    cleanup_after_visual_test();
    try {
      test.run();
      std::cout << "[PASS] " << test.name << std::endl;
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ": " << error.what() << std::endl;
    }
    cleanup_after_visual_test();
  }

  return failures == 0 ? 0 : 1;
}
