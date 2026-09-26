Unicode true
RequestExecutionLevel user
WindowIcon off
SetCompressor /SOLID lzma

!include "nsDialogs.nsh"
!include "WinMessages.nsh"
!include "LogicLib.nsh"

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
BrandingText ""

Var Dialog
Var Background
Var BackgroundHandle
Var ClickArea
Var Progress
Var ProgressText
Var PercentText

Page custom WelcomePage
Page custom InstallPage
Page custom FinishPage
UninstPage uninstConfirm
UninstPage instfiles

Function .onInit
  InitPluginsDir
  File /oname=$PLUGINSDIR\welcome.bmp "installer-welcome.bmp"
  File /oname=$PLUGINSDIR\install.bmp "installer-install.bmp"
  File /oname=$PLUGINSDIR\finish.bmp "installer-finish.bmp"
  Call StyleWindow
FunctionEnd

Function StyleWindow
  System::Call 'user32::GetWindowLongW(p $HWNDPARENT, i -16) i .r0'
  IntOp $0 $0 & 0xFF3FFFFF
  System::Call 'user32::SetWindowLongW(p $HWNDPARENT, i -16, i r0)'
  System::Call 'user32::GetSystemMetrics(i 0) i .r1'
  System::Call 'user32::GetSystemMetrics(i 1) i .r2'
  IntOp $1 $1 - 430
  IntOp $1 $1 / 2
  IntOp $2 $2 - 425
  IntOp $2 $2 / 2
  System::Call 'user32::SetWindowPos(p $HWNDPARENT, p 0, i r1, i r2, i 430, i 425, i 0x0020)'
  System::Call 'gdi32::CreateRoundRectRgn(i 0, i 0, i 430, i 425, i 20, i 20) p .r0'
  System::Call 'user32::SetWindowRgn(p $HWNDPARENT, p r0, i 1)'
FunctionEnd

Function HideNavigation
  GetDlgItem $0 $HWNDPARENT 1
  ShowWindow $0 ${SW_HIDE}
  GetDlgItem $0 $HWNDPARENT 2
  ShowWindow $0 ${SW_HIDE}
  GetDlgItem $0 $HWNDPARENT 3
  ShowWindow $0 ${SW_HIDE}
FunctionEnd

Function AddBackground
  Exch $0
  ${NSD_CreateBitmap} 0 0 430px 425px ""
  Pop $Background
  ${NSD_SetImage} $Background $0 $BackgroundHandle
FunctionEnd

Function AddWindowControls
  ${NSD_CreateLabel} 383px 8px 38px 36px ""
  Pop $ClickArea
  SetCtlColors $ClickArea "" transparent
  ${NSD_OnClick} $ClickArea CloseInstaller
  ${NSD_CreateLabel} 340px 8px 38px 36px ""
  Pop $ClickArea
  SetCtlColors $ClickArea "" transparent
  ${NSD_OnClick} $ClickArea MinimizeInstaller
FunctionEnd

Function WelcomePage
  Call StyleWindow
  Call HideNavigation
  nsDialogs::Create 1018
  Pop $Dialog
  Push "$PLUGINSDIR\welcome.bmp"
  Call AddBackground
  Call AddWindowControls
  ${NSD_CreateLabel} 105px 322px 222px 49px ""
  Pop $ClickArea
  SetCtlColors $ClickArea "" transparent
  ${NSD_OnClick} $ClickArea StartInstallation
  nsDialogs::Show
FunctionEnd

Function StartInstallation
  GetDlgItem $0 $HWNDPARENT 1
  ShowWindow $0 ${SW_SHOW}
  SendMessage $0 ${BM_CLICK} 0 0
FunctionEnd

Function InstallPage
  Call StyleWindow
  Call HideNavigation
  nsDialogs::Create 1018
  Pop $Dialog
  Push "$PLUGINSDIR\install.bmp"
  Call AddBackground
  Call AddWindowControls
  ${NSD_CreateProgressBar} 40px 158px 315px 5px ""
  Pop $Progress
  SendMessage $Progress ${PBM_SETRANGE32} 0 100
  SendMessage $Progress ${PBM_SETPOS} 8 0
  ${NSD_CreateLabel} 363px 148px 42px 24px "8%"
  Pop $PercentText
  SetCtlColors $PercentText 0x17324D 0xF7F6F4
  CreateFont $0 "Segoe UI" 10 500
  SendMessage $PercentText ${WM_SETFONT} $0 1
  nsDialogs::CreateTimer PerformInstall 120
  nsDialogs::Show
FunctionEnd

Function PerformInstall
  nsDialogs::KillTimer PerformInstall
  SendMessage $Progress ${PBM_SETPOS} 24 0
  ${NSD_SetText} $PercentText "24%"
  SetOutPath "$INSTDIR"
  File /r "${BUILD_DIR}\*"
  SendMessage $Progress ${PBM_SETPOS} 82 0
  ${NSD_SetText} $PercentText "82%"

  WriteUninstaller "$INSTDIR\Uninstall Soulu.exe"
  WriteRegStr HKCU "Software\Soulu" "InstallDir" "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\Soulu"
  CreateShortcut "$SMPROGRAMS\Soulu\Soulu.lnk" "$INSTDIR\Soulu.exe" "" "$INSTDIR\ui\browser-app-icon.ico"
  CreateShortcut "$DESKTOP\Soulu.lnk" "$INSTDIR\Soulu.exe" "" "$INSTDIR\ui\browser-app-icon.ico"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayName" "Soulu"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayIcon" "$INSTDIR\Soulu.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "UninstallString" '"$INSTDIR\Uninstall Soulu.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "Publisher" "Soulu"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayVersion" "0.9.0-cef-preview.6"

  SendMessage $Progress ${PBM_SETPOS} 100 0
  ${NSD_SetText} $PercentText "100%"
  Sleep 350
  GetDlgItem $0 $HWNDPARENT 1
  ShowWindow $0 ${SW_SHOW}
  SendMessage $0 ${BM_CLICK} 0 0
FunctionEnd

Function FinishPage
  Call StyleWindow
  Call HideNavigation
  nsDialogs::Create 1018
  Pop $Dialog
  Push "$PLUGINSDIR\finish.bmp"
  Call AddBackground
  Call AddWindowControls
  ${NSD_CreateLabel} 108px 339px 215px 49px ""
  Pop $ClickArea
  SetCtlColors $ClickArea "" transparent
  ${NSD_OnClick} $ClickArea OpenSoulu
  nsDialogs::Show
FunctionEnd

Function OpenSoulu
  Exec '"$INSTDIR\Soulu.exe"'
  Quit
FunctionEnd

Function CloseInstaller
  MessageBox MB_YESNO|MB_ICONQUESTION "Прервать установку Soulu?" IDNO +2
  Quit
FunctionEnd

Function MinimizeInstaller
  ShowWindow $HWNDPARENT ${SW_MINIMIZE}
FunctionEnd

Section
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\Soulu.lnk"
  RMDir /r "$SMPROGRAMS\Soulu"
  DeleteRegKey HKCU "Software\Soulu"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu"
  RMDir /r "$INSTDIR"
SectionEnd
