!include "WinMessages.nsh"

!define APP_NAME "d2m3u"
!define APP_VERSION "0.3"
!define INSTALL_DIR "$PROGRAMFILES64\d2m3u"
!define UNINSTALLER "uninstall.exe"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "d2m3u-setup.exe"
InstallDir "${INSTALL_DIR}"
RequestExecutionLevel admin
ShowInstDetails show
ShowUninstDetails show

Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
    SetOutPath "$INSTDIR"

    ; CLI, GUI, and all required DLLs from the Release build
    File "x64\Release\d2m3u.exe"
    File "x64\Release\d2m3u-gui.exe"
    File "x64\Release\*.dll"

    WriteUninstaller "$INSTDIR\${UNINSTALLER}"

    ; Add install dir to PATH
    ReadRegStr $0 HKCU "Environment" "PATH"
    WriteRegExpandStr HKCU "Environment" "PATH" "$0;$INSTDIR"
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000

    ; Add to Programs and Features
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "DisplayName" "${APP_NAME} ${APP_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "UninstallString" "$INSTDIR\${UNINSTALLER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "Publisher" "fujimite (edgebug.net)"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "DisplayVersion" "${APP_VERSION}"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "NoRepair" 1
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\d2m3u.exe"
    Delete "$INSTDIR\d2m3u-gui.exe"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\${UNINSTALLER}"
    RMDir "$INSTDIR"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

    MessageBox MB_OK "You will need to manually remove $INSTDIR from your PATH environment variable."
SectionEnd