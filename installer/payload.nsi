Unicode true
RequestExecutionLevel user
SilentInstall silent
SetCompressor /SOLID lzma
!ifndef BUILD_DIR
!error "BUILD_DIR required"
!endif
!ifndef OUT_FILE
!error "OUT_FILE required"
!endif
Name "Soulu"
OutFile "${OUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Soulu"
Icon "${BUILD_DIR}\ui\browser-app-icon.ico"
UninstallIcon "${BUILD_DIR}\ui\browser-app-icon.ico"
Section
  nsExec::ExecToStack /TIMEOUT=5000 'taskkill /F /IM Soulu.exe'
  Pop $0
  Pop $1
  Sleep 200
  ClearErrors
  SetOutPath "$INSTDIR"
  SetOverwrite on
  File /r "${BUILD_DIR}\*"
  IfErrors install_failed
  WriteUninstaller "$INSTDIR\Uninstall Soulu.exe"
  WriteRegStr HKCU "Software\Soulu" "InstallDir" "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\Soulu"
  CreateShortcut "$SMPROGRAMS\Soulu\Soulu.lnk" "$INSTDIR\Soulu.exe" "" "$INSTDIR\ui\browser-app-icon.ico"
  CreateShortcut "$DESKTOP\Soulu.lnk" "$INSTDIR\Soulu.exe" "" "$INSTDIR\ui\browser-app-icon.ico"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayName" "Soulu"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayIcon" "$INSTDIR\Soulu.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "UninstallString" '$"$INSTDIR\Uninstall Soulu.exe$"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "Publisher" "Soulu"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayVersion" "0.9.0-cef-preview.17"
  IfErrors install_failed
  SetErrorLevel 0
  Goto done
install_failed:
  SetErrorLevel 2
done:
SectionEnd
UninstPage uninstConfirm
UninstPage instfiles
Section "Uninstall"
  Delete "$DESKTOP\Soulu.lnk"
  RMDir /r "$SMPROGRAMS\Soulu"
  DeleteRegKey HKCU "Software\Soulu"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu"
  RMDir /r "$INSTDIR"
SectionEnd
