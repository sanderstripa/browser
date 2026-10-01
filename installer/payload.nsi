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
Name "Soulu Preview 40"
OutFile "${OUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Soulu"
Icon "${BUILD_DIR}\ui\soulu-icon.ico"
UninstallIcon "${BUILD_DIR}\ui\soulu-icon.ico"
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
  SetOutPath "$INSTDIR\ui"
  File /oname=soulu-icon-v24.ico "${BUILD_DIR}\ui\soulu-icon.ico"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall Soulu.exe"
  WriteRegStr HKCU "Software\Soulu" "InstallDir" "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\Soulu"
  Delete "$SMPROGRAMS\Soulu\Soulu.lnk"
  Delete "$DESKTOP\Soulu.lnk"
  CreateShortcut "$SMPROGRAMS\Soulu\Soulu.lnk" "$INSTDIR\Soulu.exe" "" "$INSTDIR\ui\soulu-icon-v24.ico" 0 SW_SHOWNORMAL
  CreateShortcut "$DESKTOP\Soulu.lnk" "$INSTDIR\Soulu.exe" "" "$INSTDIR\ui\soulu-icon-v24.ico" 0 SW_SHOWNORMAL
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayName" "Soulu"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayIcon" "$INSTDIR\Soulu.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "UninstallString" '$"$INSTDIR\Uninstall Soulu.exe$"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "Publisher" "Soulu"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayVersion" "0.9.0-cef-preview.40"
  System::Call 'shell32::SHChangeNotify(i 0x00002000, i 0x0005, w "$INSTDIR\Soulu.exe", p 0)'
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
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
