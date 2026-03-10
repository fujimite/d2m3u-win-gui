@echo off
setlocal EnableDelayedExpansion

set MSYS2=C:\msys64
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Debug

set OUT_DIR=%~2
if "%OUT_DIR%"=="" set OUT_DIR=%~dp0x64\%CONFIG%\

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

:: Convert paths to Unix-style for MSYS2
set CLI_DIR=%~dp0d2m3u
set CLI_DIR_UNIX=%CLI_DIR:\=/%
set CLI_DIR_UNIX=/c/%CLI_DIR_UNIX:~3%

set OUT_DIR_UNIX=%OUT_DIR:\=/%
set OUT_DIR_UNIX=/c/%OUT_DIR_UNIX:~3%

echo Building d2m3u.exe (%CONFIG%) via MinGW...
echo CLI_DIR:  %CLI_DIR%
echo OUT_DIR:  %OUT_DIR%

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

:: Release-only: copy licenses into output dir and build installer
if /i "%CONFIG%"=="Release" (
    set LICENSES_SRC=%~dp0d2m3u\dist\licenses
    set LICENSES_DST=%OUT_DIR%licenses\

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

echo CLI build complete. Output: %OUT_DIR%