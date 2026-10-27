#pragma once

// Seeds a StackWalk64 frame from a thread context for the test harnesses' crash and
// hang reports, on whichever 64-bit architecture the tests were built for (x64, or
// native ARM64 on Windows on Arm). Returns the machine type StackWalk64 takes.
// Include after <windows.h> and <dbghelp.h>.

inline DWORD init_stack_walk_frame(const CONTEXT& context, STACKFRAME64& frame) {
  frame = {};
#if defined(_M_ARM64)
  frame.AddrPC.Offset = context.Pc;
  frame.AddrFrame.Offset = context.Fp;
  frame.AddrStack.Offset = context.Sp;
  constexpr DWORD machine = IMAGE_FILE_MACHINE_ARM64;
#else
  frame.AddrPC.Offset = context.Rip;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrStack.Offset = context.Rsp;
  constexpr DWORD machine = IMAGE_FILE_MACHINE_AMD64;
#endif
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Mode = AddrModeFlat;
  return machine;
}
