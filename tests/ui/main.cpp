
#include "core/adjustment_layer.hpp"
#include "synthetic_dng.hpp"
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
// Crash handlers, bootstrap, and the runner loop moved verbatim from
// tests/ui_visual_tests.cpp. main() concatenates the group registration
// functions in the exact original registration-vector order.
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "core/pixel_depth.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/localization.hpp"
#include "ui/psd_font_resolver.hpp"
#include "ui/theme_manager.hpp"
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QMessageBox>
#include <QMetaObject>
#include <QSettings>
#include <QString>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <future>
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

// Under AddressSanitizer the sanitizer runtime must own SIGSEGV/SIGBUS: its report
// (with alloc/free stacks) is strictly better than our raw backtrace, and installing
// ours on top can suppress it.
#if defined(__SANITIZE_ADDRESS__)
#define PATCHY_ASAN_ACTIVE 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PATCHY_ASAN_ACTIVE 1
#endif
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
  // The walk above stops at the first frame it cannot unwind (a fault inside
  // the heap manager prints nothing), so also leave a minidump beside the
  // artifacts: `dump_stack.exe <dmp> build\release` symbolizes it offline
  // against the matching PDB (docs/testing.md).
  char dump_path[MAX_PATH] = {};
  snprintf(dump_path, sizeof(dump_path), "test-artifacts\\crash-%lu.dmp",
           static_cast<unsigned long>(GetCurrentProcessId()));
  CreateDirectoryA("test-artifacts", nullptr);
  const auto dump_file =
      CreateFileA(dump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (dump_file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION exception_info = {};
    exception_info.ThreadId = GetCurrentThreadId();
    exception_info.ExceptionPointers = info;
    exception_info.ClientPointers = FALSE;
    const auto dump_type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs |
                                                      MiniDumpWithHandleData | MiniDumpWithThreadInfo);
    const auto written = MiniDumpWriteDump(process, GetCurrentProcessId(), dump_file, dump_type, &exception_info,
                                           nullptr, nullptr);
    CloseHandle(dump_file);
    fprintf(stderr, written != FALSE ? "[CRASH] minidump written to %s\n" : "[CRASH] minidump failed: %s\n",
            dump_path);
  }
  fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// Names the windows open when a test times out (test_harness.hpp TestWatchdog); a
// modal dialog nobody answers is the usual cause. The list is built on the GUI thread,
// which a modal exec() still serves, so the blocking dialog names itself. A GUI thread
// that never answers is busy or deadlocked rather than waiting in an event loop.
void report_open_windows() {
  auto listing = std::make_shared<std::promise<std::string>>();
  auto answer = listing->get_future();
  QMetaObject::invokeMethod(
      qApp,
      [listing] {
        std::string text;
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (!widget->isVisible()) {
            continue;
          }
          text += std::string("  open window: ") + widget->metaObject()->className() + " objectName='" +
                  widget->objectName().toStdString() + "' title='" + widget->windowTitle().toStdString() + "'" +
                  (widget->isModal() ? " modal" : "") + '\n';
        }
        if (auto* modal = QApplication::activeModalWidget(); modal != nullptr) {
          text += std::string("  active modal: ") + modal->metaObject()->className() + " objectName='" +
                  modal->objectName().toStdString() + "'\n";
        }
        listing->set_value(text.empty() ? std::string("  no visible windows\n") : text);
      },
      Qt::QueuedConnection);
  if (answer.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
    fputs(answer.get().c_str(), stderr);
  } else {
    fputs("  the GUI thread did not answer within 5 s: busy or deadlocked, not waiting in an event loop\n", stderr);
  }
}

#ifndef Q_OS_WIN
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
  // Tests written for 8-bit opening keep it; deep tests override the gate themselves.
  patchy::set_deep_editing_default(false);
#ifdef Q_OS_WIN
  AddVectoredExceptionHandler(1, report_access_violation);
#else
#ifndef PATCHY_ASAN_ACTIVE
  struct sigaction crash_action {};
  crash_action.sa_sigaction = report_fatal_signal;
  crash_action.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &crash_action, nullptr);
  sigaction(SIGBUS, &crash_action, nullptr);
#endif
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
  // PATCHY_UI_TEST_PLATFORM=<qpa plugin> runs the suite on a real platform (cocoa, windows,
  // xcb) instead of offscreen: the way to reach native menubar and window-activation code
  // (GitHub issue 29). Screens and fonts then differ, so run a filter, not the whole suite.
  const QByteArray native_platform = qgetenv("PATCHY_UI_TEST_PLATFORM");
  qputenv("QT_QPA_PLATFORM", native_platform.isEmpty() ? QByteArray("offscreen") : native_platform);
  QApplication app(argc, argv);
  // Child mode for ui_bundled_web_fonts_register_and_create_engines: register and
  // validate the bundled web-font inventory without polluting the parent suite's
  // font database (application fonts are never removed at runtime). Must return
  // before the QSettings bootstrap below so the child never rewrites the store
  // the parent suite is using.
  if (argc > 1 && std::string(argv[1]) == "--bundled-web-fonts-probe") {
    return patchy::test::ui::run_bundled_web_fonts_probe();
  }
  app.setFont(visual_test_font());
  // The app installs this at startup, so the suite must too: imported-PSD text tests
  // resolve PostScript font names against whatever fonts each test has registered.
  patchy::ui::install_font_database_psd_font_resolver();
  ensure_artifact_dir();
  const auto test_settings_path = QDir::current().filePath(QStringLiteral("test-artifacts/settings"));
  CHECK(QDir().mkpath(test_settings_path));
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, test_settings_path);
  // The Linux suite runs inside the Flatpak SDK sandbox (docs/linux-testing.md), which
  // sets FLATPAK_ID to the SDK's id. This binary is not a Flatpak app: without this the
  // app code takes its "I am the installed Flatpak" paths (connector command line, MCP
  // socket location) against a layout that is not there.
  qunsetenv("FLATPAK_ID");
  // Every test window owns a recovery instance folder; keep them out of the real
  // per-user AutoRecover store (a test window is never a crashed user session).
  if (qEnvironmentVariableIsEmpty("PATCHY_RECOVERY_DIR")) {
    qputenv("PATCHY_RECOVERY_DIR", QDir::current().filePath(QStringLiteral("test-artifacts/recovery")).toUtf8());
  }
  // Every MainWindow registers the user scripts' hotkey commands at construction, so the
  // suite must never see a developer's real scripts folder (a @hotkey there would change
  // the default-shortcut assertions). Per process, like the font store below.
  if (qEnvironmentVariableIsEmpty("PATCHY_USER_SCRIPTS_DIR")) {
    const auto scripts = QDir::current().filePath(QStringLiteral("test-artifacts/user-scripts/") +
                                                  QString::number(QCoreApplication::applicationPid()));
    // A pid is not unique across runs: the Flatpak SDK sandbox the Linux suite runs in
    // has its own pid namespace and hands every run the same low pids. Start empty.
    if (QFileInfo::exists(scripts)) {
      patchy::test::ui::remove_test_scratch_dir(scripts);
    }
    qputenv("PATCHY_USER_SCRIPTS_DIR", scripts.toUtf8());
  }
  // The dropped-font store is private to this PROCESS. It used to be QStandardPaths'
  // test-mode app-data folder, one directory shared by every checkout and worktree on the
  // machine. A process keeps the store files it registered open until it exits, so a second
  // suite process could not delete them in its start-of-test cleanup on Windows (the full UI
  // suite failed that way during the October 2026 1.05 release, with another session's tests
  // running), and on Linux and macOS the same cleanup deletes fonts the first process is
  // still drawing with. Stores left by processes that have exited are removed here; a lock
  // file marks the ones still in use. A lock that names this process's own pid is stale
  // too (nothing here has locked a store yet): inside a pid namespace, such as the Flatpak
  // SDK sandbox the Linux suite runs in, every run gets the same low pids, and QLockFile
  // would call the previous run's lock live because "its" process, this one, is running.
  if (qEnvironmentVariableIsEmpty("PATCHY_USER_FONTS_DIR")) {
    const QDir stores(QDir::current().filePath(QStringLiteral("test-artifacts/user-fonts")));
    for (const auto& stale : stores.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      QLockFile stale_lock(stale.absoluteFilePath() + QStringLiteral("/store.lock"));
      stale_lock.setStaleLockTime(0);
      bool unlocked = stale_lock.tryLock(0);
      if (!unlocked) {
        qint64 owner_pid = 0;
        QString owner_host;
        QString owner_app;
        if (stale_lock.getLockInfo(&owner_pid, &owner_host, &owner_app) &&
            owner_pid == QCoreApplication::applicationPid()) {
          unlocked = stale_lock.removeStaleLockFile() && stale_lock.tryLock(0);
        }
      }
      if (unlocked) {
        stale_lock.unlock();
        patchy::test::ui::remove_test_scratch_dir(stale.absoluteFilePath());
      }
    }
    const auto store = stores.filePath(QString::number(QCoreApplication::applicationPid()));
    CHECK(QDir().mkpath(store));
    // Deliberately leaked: held until the process exits, which is what frees the fonts.
    auto* store_lock = new QLockFile(store + QStringLiteral("/store.lock"));
    store_lock->setStaleLockTime(0);
    CHECK(store_lock->tryLock(0));
    qputenv("PATCHY_USER_FONTS_DIR", store.toUtf8());
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools"));
    settings.remove(QStringLiteral("view"));
    settings.remove(QStringLiteral("input"));
    settings.remove(QStringLiteral("imports"));
    settings.remove(QStringLiteral("window"));
    settings.remove(QStringLiteral("filters/gallery"));
    // The dialogs that remember their unit combos (docs/resolution-units.md); the
    // auto-accept helpers type pixel values, so a developer's real choice must not
    // seed a run.
    settings.remove(QStringLiteral("newDocument"));
    settings.remove(QStringLiteral("imageSize"));
    settings.remove(QStringLiteral("canvasSize"));
    // Hotkey tests customize-then-restore this group; a run killed in between
    // would otherwise leave the overrides in the shared store permanently and
    // fail every later default-shortcut assertion.
    settings.remove(QStringLiteral("hotkeys"));
    settings.remove(QStringLiteral("preferences/language"));
    settings.remove(QStringLiteral("preferences/colorScheme"));
    settings.setValue(QStringLiteral("updates/checkOnStartup"), false);
    // Many fixtures name fonts a test host lacks; the Missing Fonts dialog would block every
    // interactive open of them. The tests that cover it turn it on themselves.
    settings.setValue(QStringLiteral("imports/showMissingFontsDialog"), false);
    settings.setValue(QStringLiteral("text/warnMissingFontsOnTransform"), false);
    // Linked fixtures saved elsewhere would raise the Missing Linked Files dialog the same way;
    // ui_smart_object_missing_linked_files_dialog_lists_and_relinks turns it on.
    settings.setValue(QStringLiteral("imports/showMissingLinksDialog"), false);
    // The CMYK Proof notice would block every interactive open of a CMYK fixture the same way;
    // ui_cmyk_open_explains_cmyk_proof_emulation turns it on.
    settings.setValue(QStringLiteral("imports/cmykProofOpenNotice"), false);
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false);
  // Pin the suite to Dark. The offscreen platform reports Qt::ColorScheme::Unknown
  // so "follow system" would already resolve here, but pinning explicitly keeps
  // every color assertion in the corpus independent of the host's Windows setting
  // and of any future change to how Unknown resolves.
  patchy::ui::ThemeManager::instance().set_preference(patchy::ui::ColorSchemePreference::Dark,
                                                      /*persist=*/false);

  std::vector<TestCase> tests;
  for (const auto& registration : {
           app_shell_tests,
           localization_tests,
           font_coverage_tests,
           missing_fonts_tests,
           filter_catalog_dialog_tests,
           layer_style_gradient_tests,
           destructive_filters_gallery_tests,
           pickers_notices_hotkeys_tests,
           canvas_view_tools_tests,
           layer_context_lifecycle_tests,
           brush_pattern_palette_tests,
           layer_panel_organization_tests,
           layer_effect_rows_tests,
           move_tool_processing_overlay_tests,
           move_transform_zones_tests,
           free_transform_distort_tests,
           alignment_tests,
           selection_marquee_lasso_tests,
           crop_tool_tests,
           unit_spin_box_tests,
           clipboard_free_transform_tests,
           group_transform_tests,
           channels_panel_tests,
           camera_raw_heif_tests,
           layer_mask_tests,
           pen_tablet_input_tests,
           brush_engine_stroke_tests,
           text_editor_font_picker_tests,
           psd_text_import_tests,
           text_transform_commit_tests,
           text_vertical_rtl_tests,
           text_area_tests,
           flat_image_format_tests,
           smart_filter_tests,
           smart_object_tests,
           warp_tests,
           import_print_resolution_tests,
           divide_photos_tests,
           folder_open_export_tests,
           artboard_ui_tests,
           image_adjustments_curves_tests,
           selection_engines_tests,
           ai_selection_ui_tests,
           misc_visuals_outline_stress_tests,
           float_window_tests,
           vector_shape_tool_tests,
           batch_appearance_tests,
           vector_preview_tests,
           vector_point_editing_tests,
           vector_commands_tests,
           layer_merge_tests,
           vector_scripting_tests,
           svg_ui_tests,
           image_trace_ui_tests,
           scripting_tests,
           script_move_tests,
           document_recovery_tests,
           document_rename_tests,
           mcp_tests,
           unicode_path_tests,
           legacy_plugin_tests,
           gmic_ui_tests,
           history_panel_tests,
           history_stroke_patch_tests,
           file_info_tests,
           layered_tiff_ui_tests,
           appimage_menu_entry_tests,
           composite_render_tests,
           readme_screenshot_tests,
       }) {
    auto group = registration();
    tests.insert(tests.end(), std::make_move_iterator(group.begin()),
                 std::make_move_iterator(group.end()));
  }

  // Proves the per-test timeout fires on a dialog nobody answers: run with
  // PATCHY_TEST_TIMEOUT_SELFTEST=1 and a short PATCHY_TEST_TIMEOUT_SECONDS; the run must
  // end with this test's timeout [FAIL] line naming timeoutSelftestDialog.
  if (qgetenv("PATCHY_TEST_TIMEOUT_SELFTEST") == "1") {
    tests.push_back({"ui_test_harness_timeout_selftest", [] {
                       QMessageBox box(QMessageBox::Information, QStringLiteral("Unanswered"),
                                       QStringLiteral("Nobody answers this dialog."));
                       box.setObjectName(QStringLiteral("timeoutSelftestDialog"));
                       box.exec();
                     }});
  }

  // The filter is a comma-separated list of name substrings (tests/test_harness.hpp,
  // run_test_suite); ordered cross-test repros pick the polluting test and the victim
  // in one run. The command line's filter wins over PATCHY_UI_TEST_FILTER.
  patchy::test::TestSuiteOptions options;
  options.default_filter = qgetenv("PATCHY_UI_TEST_FILTER").toStdString();
  options.timeout_seconds = patchy::test::test_timeout_seconds(600);
  options.timeout_report = report_open_windows;
  options.around_each = cleanup_after_visual_test;
  const int result = patchy::test::run_test_suite(tests, argc, argv, options);

  // Detached preview/render workers capture the QApplication pointer; wait
  // for them before the suite's QApplication is destroyed (the same shutdown
  // latch src/app/main.cpp uses).
  patchy::ui::wait_for_tracked_background_workers();
  return result;
}
