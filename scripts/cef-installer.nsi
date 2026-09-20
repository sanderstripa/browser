Unicode true
RequestExecutionLevel user

!include "MUI2.nsh"

!ifndef BUILD_DIR
  !error "BUILD_DIR is required"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "Soulu-CEF-Setup.exe"
!endif

Name "Soulu"
OutFile "${OUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Soulu"
InstallDirRegKey HKCU "Software\Soulu" "InstallDir"
Icon "${BUILD_DIR}\ui\browser-app-icon.ico"
UninstallIcon "${BUILD_DIR}\ui\browser-app-icon.ico"
SetCompressor /SOLID lzma
BrandingText "SOULU  •  ЛЕГЧЕ  •  ЧИЩЕ  •  ЯРЧЕ"
ManifestDPIAware true

!define MUI_ICON "${BUILD_DIR}\ui\browser-app-icon.ico"
!define MUI_UNICON "${BUILD_DIR}\ui\browser-app-icon.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Спокойный интернет впереди."
!define MUI_WELCOMEPAGE_TEXT "Установи Soulu и открой больше возможностей.$\r$\n$\r$\nБыстро. Чисто. Без вмешательства в системный прокси Windows."
!define MUI_INSTFILESPAGE_FINISHHEADER_TEXT "Установка завершена"
!define MUI_INSTFILESPAGE_FINISHHEADER_SUBTEXT "Soulu готов к первому запуску."
!define MUI_FINISHPAGE_TITLE "Всё готово!"
!define MUI_FINISHPAGE_TEXT "Soulu успешно установлен.$\r$\n$\r$\nПора исследовать более спокойный интернет."
!define MUI_FINISHPAGE_RUN "$INSTDIR\Soulu.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Открыть Soulu"
!define MUI_FINISHPAGE_LINK "Открыть страницу проекта"
!define MUI_FINISHPAGE_LINK_LOCATION "https://github.com/sanderstripa/browser"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "English"

Section "Soulu" SEC_MAIN
  SetOutPath "$INSTDIR"
  File /r "${BUILD_DIR}\*"
  WriteUninstaller "$INSTDIR\Uninstall Soulu.exe"

  WriteRegStr HKCU "Software\Soulu" "InstallDir" "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\Soulu"
  CreateShortcut "$SMPROGRAMS\Soulu\Soulu.lnk" "$INSTDIR\Soulu.exe" "" "$INSTDIR\ui\browser-app-icon.ico"
  CreateShortcut "$DESKTOP\Soulu.lnk" "$INSTDIR\Soulu.exe" "" "$INSTDIR\ui\browser-app-icon.ico"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayName" "Soulu"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayIcon" "$INSTDIR\Soulu.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "UninstallString" '"$INSTDIR\Uninstall Soulu.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "Publisher" "Soulu"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayVersion" "0.9.0-cef-preview.3"
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\Soulu.lnk"
  RMDir /r "$SMPROGRAMS\Soulu"
  DeleteRegKey HKCU "Software\Soulu"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu"
  RMDir /r "$INSTDIR"
SectionEnd
