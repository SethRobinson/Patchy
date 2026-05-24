param(
  [string]$BuildDir = "build/app",
  [string]$QtPrefix = ".deps/Qt/6.8.3/msvc2022_64"
)

$ErrorActionPreference = "Stop"

function Invoke-CheckedCmd {
  param([string]$Command)
  cmd.exe /s /c $Command
  if ($LASTEXITCODE -ne 0) {
    throw "Command failed with exit code $LASTEXITCODE`: $Command"
  }
}

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
$vsDev = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"

if (!(Test-Path $cmake)) {
  $cmake = "cmake"
}
if (!(Test-Path $ctest)) {
  $ctest = "ctest"
}

$prefix = Join-Path $repo $QtPrefix
$build = Join-Path $repo $BuildDir
$configure = "`"$vsDev`" -arch=x64 && `"$cmake`" -S `"$repo`" -B `"$build`" -G Ninja -DPHOTOSLOP_BUILD_APP=ON -DPHOTOSLOP_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=`"$prefix`""
$buildCommand = "`"$vsDev`" -arch=x64 && `"$cmake`" --build `"$build`""
$deploy = Join-Path $prefix "bin/windeployqt.exe"
$app = Join-Path $build "photoslop.exe"
$testCommand = "`"$vsDev`" -arch=x64 && `"$ctest`" --test-dir `"$build`" --output-on-failure"

Invoke-CheckedCmd $configure
Invoke-CheckedCmd $buildCommand
if ((Test-Path $deploy) -and (Test-Path $app)) {
  Invoke-CheckedCmd "`"$vsDev`" -arch=x64 && `"$deploy`" `"$app`""
  $platforms = Join-Path $build "platforms"
  New-Item -ItemType Directory -Force -Path $platforms | Out-Null
  foreach ($plugin in @("qoffscreen.dll", "qoffscreend.dll", "qminimal.dll", "qminimald.dll")) {
    $source = Join-Path $prefix "plugins/platforms/$plugin"
    if (Test-Path $source) {
      Copy-Item -Force $source $platforms
    }
  }
}
Invoke-CheckedCmd $testCommand
