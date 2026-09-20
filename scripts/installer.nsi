Unicode True
SetCompressor /SOLID lzma

!define APP_NAME "Soulu"
!define APP_VERSION "0.8.0"
!define APP_PUBLISHER "Sander Stripa"
!define APP_EXE "Soulu.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu"

Name "${APP_NAME}"
Caption "Установка ${APP_NAME}"
OutFile "..\release\Soulu-Setup-0.8.0-x64.exe"
InstallDir "$LOCALAPPDATA\Programs\Soulu"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel user
Icon "..\ui\soulu-installer-icon.ico"
BrandingText "${APP_NAME}"
ShowInstDetails nevershow
ShowUninstDetails nevershow

VIProductVersion "0.8.0.0"
VIAddVersionKey /LANG=1049 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1049 "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey /LANG=1049 "FileDescription" "Установщик ${APP_NAME}"
VIAddVersionKey /LANG=1049 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1049 "ProductVersion" "${APP_VERSION}"

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File /r "..\release\installer-stage\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\Soulu"
  CreateShortcut "$SMPROGRAMS\Soulu\Soulu.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$SMPROGRAMS\Soulu\Удалить Soulu.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\Soulu.lnk" "$INSTDIR\${APP_EXE}"

  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  Delete "$DESKTOP\Soulu.lnk"
  RMDir /r "$SMPROGRAMS\Soulu"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  RMDir /r "$INSTDIR"
SectionEnd
