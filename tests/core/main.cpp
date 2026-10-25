#include "test_groups.hpp"
#include "test_harness.hpp"
#include "core/pixel_depth.hpp"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>
#ifndef __EMSCRIPTEN__
#include <chrono>
#include <thread>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#elif defined(__EMSCRIPTEN__)
// No crash reporter under wasm: Emscripten has neither execinfo.h nor fatal
// signal delivery, and node already prints a wasm stack for traps and aborts.
#else
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
#endif

using patchy::test::TestCase;

#ifdef _WIN32
// Print a symbolized stack when the suite hits an access violation (ported
// from tests/ui/main.cpp). The process still dies (and WER still writes its
// dump), but the [PASS] log then ends with the faulting stack instead of
// stopping silently, which is the difference between a fixable report and an
// unreproduced flake.
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
#elif !defined(__EMSCRIPTEN__)
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

int main(int argc, char** argv) {
  patchy::test::suppress_crash_dialogs();
  // Tests written for 8-bit opening keep it; deep tests override the gate themselves.
  patchy::set_deep_editing_default(false);
#ifdef _WIN32
  AddVectoredExceptionHandler(1, report_access_violation);
#elif !defined(__EMSCRIPTEN__)
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
  std::vector<TestCase> tests;
  for (const auto& registration : {
           document_model_tests,
           compositor_blend_if_tests,
           compositor_layer_styles_tests,
           gradients_interior_effects_tests,
           psd_core_io_tests,
           stroke_mask_effects_tests,
           smart_objects_warp_tests,
           smart_filter_pixels_tests,
           smart_filter_classic_tests,
           smart_filter_descriptors_tests,
           smart_filter_distort_stylize_tests,
           smart_filter_table_tests,
           smart_filter_adjustment_tests,
           smart_filter_gallery_tests,
           psd_writer_stability_tests,
           pattern_styles_fixtures_tests,
           adjustments_curves_tests,
           psd_structure_tests,
           psd_text_tests,
           psd_legacy_text_tests,
           text_engine_block_tests,
           layer_metadata_tests,
           layer_alignment_tests,
           brush_engine_tests,
           pat_asl_abr_tests,
           pixel_tools_tests,
           palette_tests,
           document_ops_filters_tests,
           builtin_filter_looks_tests,
           flat_formats_bmp_tests,
           raw_heif_tests,
           jxr_tests,
           rttex_tests,
           dds_tests,
           ora_tests,
           jxl_tests,
           hdr_import_tests,
           flat_formats_misc_tests,
           unicode_path_tests,
           font_zip_tests,
           infra_selection_tests,
           pipl_tests,
           vector_shape_tests,
           vector_raster_tests,
           image_trace_tests,
           photo_divide_tests,
           psd_vector_fixtures_tests,
           svg_tests,
           pdf_tests,
           artboard_tests,
           af_format_tests,
           pdn_tests,
           xcf_tests,
           kra_tests,
           composite_corpus_tests,
           translation_marker_tests,
           floating_selection_tests,
           atomic_write_recovery_tests,
           pixel_depth_tests,
           psd_deep_io_tests,
           psd_cmyk_proof_tests,
           rgb_profile_tests,
           deep_compositor_tests,
           deep_filter_tests,
           ai_selection_tests,
           gmic_tests,
           file_info_metadata_tests,
           invisible_watermark_tests,
           trustmark_tests,
           tiff_layers_tests,
           hdr_display_tests,
           red_eye_tests,
       }) {
    auto group = registration();
    tests.insert(tests.end(), std::make_move_iterator(group.begin()),
                 std::make_move_iterator(group.end()));
  }

  // Proves the timeout below fires: run with PATCHY_TEST_TIMEOUT_SELFTEST=1 and a short
  // PATCHY_TEST_TIMEOUT_SECONDS; the run must end with this test's timeout [FAIL] line.
  if (patchy::test::environment_value("PATCHY_TEST_TIMEOUT_SELFTEST") == "1") {
    tests.push_back({"test_harness_timeout_selftest", [] {
#ifndef __EMSCRIPTEN__
                       std::this_thread::sleep_for(std::chrono::hours(1));
#endif
                     }});
  }

  patchy::test::TestSuiteOptions options;
  options.timeout_seconds = patchy::test::test_timeout_seconds(300);
  return patchy::test::run_test_suite(tests, argc, argv, options);
}
