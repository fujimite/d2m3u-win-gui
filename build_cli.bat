@echo off
setlocal EnableDelayedExpansion
set MSYS2=C:\msys64
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Debug
set OUT_DIR=%~2
if "%OUT_DIR%"=="" set OUT_DIR=%~dp0x64\%CONFIG%\
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

:: Skip build if d2m3u.exe already exists in the output directory
if exist "%OUT_DIR%d2m3u.exe" (
echo d2m3u.exe already exists in %OUT_DIR% -- skipping CLI build.
goto :licenses
)

set CLI_DIR=%~dp0d2m3u

:: Convert paths to Unix-style for MSYS2. Done in pure batch (no cygpath
:: shellout) so it works regardless of which drive the repo happens to be
:: checked out on (e.g. D: on GitHub Actions runners, not just C:).
call :winToUnix "%CLI_DIR%" CLI_DIR_UNIX
call :winToUnix "%OUT_DIR%" OUT_DIR_UNIX

echo Building d2m3u.exe (%CONFIG%) via MinGW...
echo CLI_DIR: %CLI_DIR% (%CLI_DIR_UNIX%)
echo OUT_DIR: %OUT_DIR% (%OUT_DIR_UNIX%)

:: Explicitly set PATH so bash gets the MinGW64 toolchain without relying
:: on --login sourcing /etc/profile correctly from a Windows parent process.
:: This guarantees gcc is the MinGW64 one, which defines _WIN32.
set PATH=%MSYS2%\mingw64\bin;%MSYS2%\usr\bin;%PATH%
set MSYSTEM=MINGW64
set CHERE_INVOKING=1

%MSYS2%\usr\bin\bash.exe -c "cd '%CLI_DIR_UNIX%' && make dist && cp -r dist/* '%OUT_DIR_UNIX%'"
if %ERRORLEVEL% neq 0 (
echo CLI build FAILED.
exit /b 1
)

goto :licenses

:winToUnix
:: %~1 = Windows path, %~2 = name of variable to receive the MSYS-style path.
:: Converts "D:\a\foo\bar" (any drive letter, any case) to "/d/a/foo/bar"
:: without any external tools.
setlocal
set "WPATH=%~1"
set "d=%WPATH:~0,1%"
set "d=%d:A=a%"
set "d=%d:B=b%"
set "d=%d:C=c%"
set "d=%d:D=d%"
set "d=%d:E=e%"
set "d=%d:F=f%"
set "d=%d:G=g%"
set "d=%d:H=h%"
set "d=%d:I=i%"
set "d=%d:J=j%"
set "d=%d:K=k%"
set "d=%d:L=l%"
set "d=%d:M=m%"
set "d=%d:N=n%"
set "d=%d:O=o%"
set "d=%d:P=p%"
set "d=%d:Q=q%"
set "d=%d:R=r%"
set "d=%d:S=s%"
set "d=%d:T=t%"
set "d=%d:U=u%"
set "d=%d:V=v%"
set "d=%d:W=w%"
set "d=%d:X=x%"
set "d=%d:Y=y%"
set "d=%d:Z=z%"
set "UPATH=%WPATH:\=/%"
set "UPATH=/%d%%UPATH:~2%"
endlocal & set "%~2=%UPATH%"
goto :eof

:licenses
:: Release-only: copy licenses into output dir and build installer
if /i "%CONFIG%"=="Release" (
set LICENSES_SRC=%~dp0d2m3u\dist\licenses
set LICENSES_DST=%OUT_DIR%licenses\
if exist "!LICENSES_DST!" (
echo Licenses already exist in !LICENSES_DST! -- skipping copy.
goto :done
)
echo Copying licenses from !LICENSES_SRC!...
if not exist "!LICENSES_SRC!" (
echo ERROR: licenses directory not found at !LICENSES_SRC!
exit /b 1
)
xcopy /E /I /Y "!LICENSES_SRC!" "!LICENSES_DST!"
if !ERRORLEVEL! neq 0 (
echo Failed to copy licenses.
exit /b 1
)
)

:done
echo CLI build complete. Output: %OUT_DIR%
