#include "test_groups.hpp"

#include "test_harness.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

#ifdef _WIN32
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

int main(int argc, char** argv) {
  patchy::test::suppress_crash_dialogs();
#ifdef _WIN32
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
           smart_filter_descriptors_tests,
           psd_writer_stability_tests,
           pattern_styles_fixtures_tests,
           adjustments_curves_tests,
           psd_structure_tests,
           psd_text_tests,
           layer_metadata_tests,
           brush_engine_tests,
           pat_asl_abr_tests,
           pixel_tools_tests,
           palette_tests,
           document_ops_filters_tests,
           flat_formats_bmp_tests,
           raw_heif_tests,
           flat_formats_misc_tests,
           infra_selection_tests,
           vector_shape_tests,
           vector_raster_tests,
           psd_vector_fixtures_tests,
           svg_tests,
       }) {
    auto group = registration();
    tests.insert(tests.end(), std::make_move_iterator(group.begin()),
                 std::make_move_iterator(group.end()));
  }

  int failures = 0;
  const std::string_view name_filter =
      argc > 1 && argv[1] != nullptr ? std::string_view(argv[1])
                                     : std::string_view{};
  for (const auto& test : tests) {
    if (!name_filter.empty() &&
        std::string_view(test.name).find(name_filter) ==
            std::string_view::npos) {
      continue;
    }
    try {
      test.run();
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
    }
  }

  return failures == 0 ? 0 : 1;
}
