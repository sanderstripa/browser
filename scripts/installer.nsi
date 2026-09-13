Unicode True
SetCompressor /SOLID lzma

!define APP_NAME "Internet Browser"
!define APP_VERSION "0.5.1"
!define APP_PUBLISHER "Sander Stripa"
!define APP_EXE "Internet Browser.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Internet Browser"

Name "${APP_NAME}"
Caption "Установка ${APP_NAME}"
OutFile "..\release\Internet-Browser-Setup-0.5.1-x64.exe"
InstallDir "$LOCALAPPDATA\Programs\Internet Browser"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel user
Icon "..\ui\browser-icon.ico"
BrandingText "${APP_NAME}"
ShowInstDetails nevershow
ShowUninstDetails nevershow

VIProductVersion "0.5.1.0"
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

  CreateDirectory "$SMPROGRAMS\Internet Browser"
  CreateShortcut "$SMPROGRAMS\Internet Browser\Internet Browser.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$SMPROGRAMS\Internet Browser\Удалить Internet Browser.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\Internet Browser.lnk" "$INSTDIR\${APP_EXE}"

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
  Delete "$DESKTOP\Internet Browser.lnk"
  RMDir /r "$SMPROGRAMS\Internet Browser"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  RMDir /r "$INSTDIR"
SectionEnd
