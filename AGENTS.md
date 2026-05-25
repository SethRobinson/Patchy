# Repository Instructions

After making code changes in this repository, create or refresh the local release build before final handoff.

Required release handoff steps:

1. Build the release preset:

   ```powershell
   cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset release'
   ```

2. Run the release test binaries from `build\release`:

   ```powershell
   .\photoslop_core_tests.exe
   $env:QT_QPA_PLATFORM='offscreen'; .\photoslop_ui_visual_tests.exe
   ```

3. In the final response, explicitly report whether the release executable exists at:

   ```text
   build\release\photoslop.exe
   ```

Do not say a release was created unless the release preset build completed successfully. If a signed installer or published release is requested, state that local packaging/signing is not currently implemented unless the repo has since added a real packaging pipeline.
