@echo off
rem Installs the signed Flatpak repository that scripts\remote\release-linux.bat built
rem beside the bundle (build\package\Patchy-<version>-flatpak-repo.tar) at
rem rtsoft.com/flatpak: what `flatpak update` reads and what the README's
rem com.rtsoft.patchy.flatpakref installs from (packaging\linux\README.md). The release
rem downloads themselves (the bundle, the AppImage, the Windows and macOS packages) are on
rem GitHub Releases only (publish-github-release.bat); rtsoft.com stopped mirroring them
rem with 1.25.
rem cd to the repo root (this script lives in scripts\release) so the relative paths
rem below resolve from any cwd.
cd /d "%~dp0..\.."
rem The ssh account comes from local config (upload-target.bat), never this file.
call "%~dp0upload-target.bat"
if errorlevel 1 (
  goto repo_fail
)

rem The repository. release-linux.ps1 refuses to finish without the tar, so a missing
rem one means the build did not complete; never publish a bundle whose repository is
rem a version behind.
set "LINUX_REPO_TAR="
for /f "delims=" %%F in ('dir /b /o:d build\package\Patchy-*-flatpak-repo.tar 2^>nul') do set "LINUX_REPO_TAR=build\package\%%F"
if not defined LINUX_REPO_TAR goto repo_missing
rem The source of the GPL and LGPL libraries the Flatpak carries goes up first, beside the
rem repository, so the binaries are never served from here without it (GPL-2 section 3:
rem source "from the same place"; docs/legal-licensing.md). The repository holds only the
rem latest commit, so only this release's zips are kept: each upload replaces the last.
if not exist build\package\PatchyGeglSources.zip goto sources_missing
if not exist build\package\PatchyLinuxScannerSources.zip goto sources_missing
ssh -n %UPLOAD_SSH% "mkdir -p www/flatpak/sources"
if errorlevel 1 goto repo_fail
call "%~dp0upload-one-file.bat" build\package\PatchyGeglSources.zip flatpak/sources
if errorlevel 1 goto repo_fail
call "%~dp0upload-one-file.bat" build\package\PatchyLinuxScannerSources.zip flatpak/sources
if errorlevel 1 goto repo_fail
curl -sfI https://rtsoft.com/flatpak/sources/PatchyGeglSources.zip >nul || goto repo_fail
curl -sfI https://rtsoft.com/flatpak/sources/PatchyLinuxScannerSources.zip >nul || goto repo_fail
echo Uploading %LINUX_REPO_TAR% as PatchyFlatpakRepo.tar
copy /y "%LINUX_REPO_TAR%" build\package\PatchyFlatpakRepo.tar >nul
if errorlevel 1 goto repo_fail
ssh -n %UPLOAD_SSH% "mkdir -p www/flatpak"
if errorlevel 1 goto repo_fail
call "%~dp0upload-one-file.bat" build\package\PatchyFlatpakRepo.tar flatpak
if errorlevel 1 goto repo_fail
rem The MIME types that make a clicked .flatpakref link open in a software center.
call "%~dp0upload-one-file.bat" packaging\linux\site\.htaccess flatpak
if errorlevel 1 goto repo_fail
rem Unpack beside the live copy, check the signed summary is there, then swap the
rem directory in with two renames so a client never reads a half-written repository.
rem The descriptor files move last: they only name the repository URL and key.
echo Installing the repository on the server ...
rem Every path is spelled out under D, which must end in /www/flatpak below a non-blank
rem HOME, so no rm -rf depends on the remote working directory (AGENTS.md).
ssh -n %UPLOAD_SSH% "D=$HOME/www/flatpak && case $D in /?*/www/flatpak) ;; *) echo bad flatpak dir: $D; exit 1 ;; esac && rm -rf $D/incoming $D/repo.old && mkdir $D/incoming && tar -xf $D/PatchyFlatpakRepo.tar -C $D/incoming && test -s $D/incoming/repo/summary && test -s $D/incoming/repo/summary.sig && test -s $D/incoming/patchy.flatpakrepo && test -s $D/incoming/com.rtsoft.patchy.flatpakref && if [ -d $D/repo ]; then mv $D/repo $D/repo.old; fi && mv $D/incoming/repo $D/repo && mv $D/incoming/patchy.flatpakrepo $D/incoming/com.rtsoft.patchy.flatpakref $D/ && rm -rf $D/incoming $D/repo.old $D/PatchyFlatpakRepo.tar"
if errorlevel 1 goto repo_fail
rem The claim "it shipped" needs evidence from the public URL, not the ssh session.
echo Verifying the live repository ...
curl -sfI https://rtsoft.com/flatpak/repo/summary >nul || goto repo_fail
curl -sfI https://rtsoft.com/flatpak/repo/summary.sig >nul || goto repo_fail
curl -sfI https://rtsoft.com/flatpak/com.rtsoft.patchy.flatpakref >nul || goto repo_fail
curl -sfI https://rtsoft.com/flatpak/patchy.flatpakrepo >nul || goto repo_fail
rem A convenience, not a correctness gate: flatpak itself ignores the type, so a host
rem without mod_mime only loses the click-to-install behavior in browsers.
curl -sI https://rtsoft.com/flatpak/com.rtsoft.patchy.flatpakref | findstr /i "application/vnd.flatpak.ref" >nul
if errorlevel 1 echo WARNING: the flatpakref is not served as application/vnd.flatpak.ref; check packaging\linux\site\.htaccess on the server.
echo Linux repository OK: https://rtsoft.com/flatpak/com.rtsoft.patchy.flatpakref
if /i not "%~1"=="nopause" pause
exit /b 0

:repo_missing
echo.
echo No Patchy-*-flatpak-repo.tar found in build\package - run scripts\remote\release-linux.bat first.
echo The Flatpak repository was NOT updated.
if /i not "%~1"=="nopause" pause
exit /b 1

:sources_missing
echo.
echo build\package must hold PatchyGeglSources.zip ^(build-release.bat^) and
echo PatchyLinuxScannerSources.zip ^(scripts\remote\release-linux.bat^): the Flatpak's GPL and
echo LGPL libraries cannot be served without their source beside them.
echo The Flatpak repository was NOT updated.
if /i not "%~1"=="nopause" pause
exit /b 1

:repo_fail
echo.
echo Flatpak repository upload FAILED - https://rtsoft.com/flatpak/repo was not updated,
echo or was left in a bad state. Installed copies will not see this release through
echo flatpak update. Do not announce this release.
if /i not "%~1"=="nopause" pause
exit /b 1
