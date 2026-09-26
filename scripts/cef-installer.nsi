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
Var InstallTimerProc
Var ProgressTrack
Var DragTimerProc
Var Dragging
Var DragOffsetX
Var DragOffsetY
Var Installed
Var PageKind
Var MouseWasDown
Var MainButton
Var MinimizeButton
Var CloseButton

Page custom WelcomePage
Page custom InstallPage
Page custom FinishPage FinishLeave
UninstPage uninstConfirm
UninstPage instfiles

Function .onInit
  InitPluginsDir
  File /oname=$PLUGINSDIR\welcome.bmp "installer-welcome.bmp"
  File /oname=$PLUGINSDIR\install.bmp "installer-install.bmp"
  File /oname=$PLUGINSDIR\finish.bmp "installer-finish.bmp"
  StrCpy $Dragging 0
  StrCpy $Installed 0
  StrCpy $PageKind 0
  StrCpy $MouseWasDown 0
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
  System::Call 'gdi32::CreateRoundRectRgn(i 0, i 0, i 431, i 426, i 20, i 20) p .r0'
  System::Call 'user32::SetWindowRgn(p $HWNDPARENT, p r0, i 1)'
FunctionEnd

Function HideNavigation
  GetDlgItem $0 $HWNDPARENT 1
  ShowWindow $0 ${SW_HIDE}
  GetDlgItem $0 $HWNDPARENT 2
  ShowWindow $0 ${SW_HIDE}
  GetDlgItem $0 $HWNDPARENT 3
  ShowWindow $0 ${SW_HIDE}
  GetDlgItem $0 $HWNDPARENT 1028
  ShowWindow $0 ${SW_HIDE}
  GetDlgItem $0 $HWNDPARENT 1034
  ShowWindow $0 ${SW_HIDE}
  GetDlgItem $0 $HWNDPARENT 1035
  ShowWindow $0 ${SW_HIDE}
FunctionEnd

Function AddBackground
  Exch $0
  ${NSD_CreateBitmap} 0 0 430px 425px ""
  Pop $Background
  ${NSD_SetImage} $Background $0 $BackgroundHandle
  System::Call 'user32::SetWindowPos(p $Background, p 1, i 0, i 0, i 0, i 0, i 0x0013)'
FunctionEnd

Function EnableClick
  Exch $0
  System::Call 'user32::GetWindowLongW(p r0, i -16) i .r1'
  IntOp $1 $1 | 0x00000100
  System::Call 'user32::SetWindowLongW(p r0, i -16, i r1)'
  Pop $0
FunctionEnd

Function StartDragTimer
  GetFunctionAddress $0 DragWindow
  StrCpy $DragTimerProc $0
  nsDialogs::CreateTimer $DragTimerProc 16
FunctionEnd

Function DragWindow
  System::Call 'user32::GetAsyncKeyState(i 0x01) i .r6'
  IntOp $6 $6 & 0x8000
  System::Call 'user32::GetCursorPos(*i .r0, *i .r1)'
  System::Call 'user32::GetWindowRect(p $HWNDPARENT, *i .r2, *i .r3, *i .r4, *i .r5)'
  ${If} $6 = 0
    StrCpy $MouseWasDown 0
    StrCpy $Dragging 0
    System::Call 'user32::ReleaseCapture()'
    Return
  ${EndIf}
  ${If} $Dragging = 1
    IntOp $2 $0 - $DragOffsetX
    IntOp $3 $1 - $DragOffsetY
    System::Call 'user32::SetWindowPos(p $HWNDPARENT, p 0, i r2, i r3, i 0, i 0, i 0x0015)'
    Return
  ${EndIf}
  ${If} $MouseWasDown = 1
    Return
  ${EndIf}
  StrCpy $MouseWasDown 1
  IntOp $4 $4 - 90
  IntOp $5 $3 + 55
  ${If} $0 >= $2
  ${AndIf} $0 < $4
  ${AndIf} $1 >= $3
  ${AndIf} $1 < $5
    StrCpy $Dragging 1
    IntOp $DragOffsetX $0 - $2
    IntOp $DragOffsetY $1 - $3
    System::Call 'user32::SetCapture(p $HWNDPARENT)'
  ${EndIf}
FunctionEnd

Function AddWindowControls
  ; Use NSIS's own Cancel control for close so mouse and keyboard share
  ; the same native event path.
  GetDlgItem $CloseButton $HWNDPARENT 2
  SendMessage $CloseButton ${WM_SETTEXT} 0 "STR:×"
  System::Call 'user32::SetWindowPos(p $CloseButton, p 0, i 384, i 8, i 36, i 32, i 0x0014)'
  ShowWindow $CloseButton ${SW_SHOW}
  EnableWindow $CloseButton 1

  ${NSD_CreateButton} 342px 8px 36px 32px "—"
  Pop $MinimizeButton
  ${NSD_OnClick} $MinimizeButton MinimizeInstaller
FunctionEnd

Function WelcomePage
  StrCpy $PageKind 1
  Call StyleWindow
  Call HideNavigation
  nsDialogs::Create 1018
  Pop $Dialog
  System::Call 'user32::SetWindowPos(p $Dialog, p 0, i 0, i 0, i 430, i 425, i 0x0014)'
  Push "$PLUGINSDIR\welcome.bmp"
  Call AddBackground
  Call AddWindowControls
  Call StartDragTimer
  ; Reuse the native Next button. Enter already targets this exact control,
  ; so a mouse click now follows the identical, reliable NSIS path.
  GetDlgItem $MainButton $HWNDPARENT 1
  SendMessage $MainButton ${WM_SETTEXT} 0 "STR:Установить  →"
  System::Call 'user32::SetWindowPos(p $MainButton, p 0, i 105, i 322, i 222, i 49, i 0x0014)'
  SetCtlColors $MainButton 0xFFFFFF 0x17324D
  ShowWindow $MainButton ${SW_SHOW}
  EnableWindow $MainButton 1
  nsDialogs::Show
  nsDialogs::KillTimer $DragTimerProc
FunctionEnd

Function InstallPage
  StrCpy $PageKind 2
  Call StyleWindow
  Call HideNavigation
  nsDialogs::Create 1018
  Pop $Dialog
  System::Call 'user32::SetWindowPos(p $Dialog, p 0, i 0, i 0, i 430, i 425, i 0x0014)'
  Push "$PLUGINSDIR\install.bmp"
  Call AddBackground
  Call AddWindowControls
  Call StartDragTimer
  ${NSD_CreateLabel} 40px 158px 315px 6px ""
  Pop $ProgressTrack
  SetCtlColors $ProgressTrack 0xD8E1EA 0xD8E1EA
  ${NSD_CreateLabel} 40px 158px 25px 6px ""
  Pop $Progress
  SetCtlColors $Progress 0x17324D 0x17324D
  ${NSD_CreateLabel} 363px 148px 42px 24px "8%"
  Pop $PercentText
  SetCtlColors $PercentText 0x17324D 0xF7F6F4
  CreateFont $0 "Segoe UI" 10 500
  SendMessage $PercentText ${WM_SETFONT} $0 1
  GetFunctionAddress $InstallTimerProc PerformInstall
  nsDialogs::CreateTimer $InstallTimerProc 120
  nsDialogs::Show
  nsDialogs::KillTimer $DragTimerProc
FunctionEnd

Function PerformInstall
  nsDialogs::KillTimer $InstallTimerProc
  System::Call 'user32::SetWindowPos(p $Progress, p 0, i 40, i 158, i 95, i 6, i 0x0014)'
  ${NSD_SetText} $PercentText "30%"
  System::Call 'user32::UpdateWindow(p $Dialog)'
  Sleep 120
  nsExec::ExecToStack /TIMEOUT=5000 'taskkill /F /IM Soulu.exe'
  Pop $0
  Pop $1
  Sleep 250
  SetOverwrite on
  SetOutPath "$INSTDIR"
  File /r "${BUILD_DIR}\*"
  System::Call 'user32::SetWindowPos(p $Progress, p 0, i 40, i 158, i 277, i 6, i 0x0014)'
  ${NSD_SetText} $PercentText "88%"
  System::Call 'user32::UpdateWindow(p $Dialog)'

  WriteUninstaller "$INSTDIR\Uninstall Soulu.exe"
  WriteRegStr HKCU "Software\Soulu" "InstallDir" "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\Soulu"
  CreateShortcut "$SMPROGRAMS\Soulu\Soulu.lnk" "$INSTDIR\Soulu.exe" "" "$INSTDIR\ui\browser-app-icon.ico"
  CreateShortcut "$DESKTOP\Soulu.lnk" "$INSTDIR\Soulu.exe" "" "$INSTDIR\ui\browser-app-icon.ico"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayName" "Soulu"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayIcon" "$INSTDIR\Soulu.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "UninstallString" '"$INSTDIR\Uninstall Soulu.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "Publisher" "Soulu"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Soulu" "DisplayVersion" "0.9.0-cef-preview.12"

  System::Call 'user32::SetWindowPos(p $Progress, p 0, i 40, i 158, i 315, i 6, i 0x0014)'
  ${NSD_SetText} $PercentText "100%"
  StrCpy $Installed 1
  System::Call 'user32::UpdateWindow(p $Dialog)'
  Sleep 350
  GetDlgItem $0 $HWNDPARENT 1
  ShowWindow $0 ${SW_SHOW}
  SendMessage $0 ${BM_CLICK} 0 0
FunctionEnd

Function FinishPage
  StrCpy $PageKind 3
  Call StyleWindow
  Call HideNavigation
  nsDialogs::Create 1018
  Pop $Dialog
  System::Call 'user32::SetWindowPos(p $Dialog, p 0, i 0, i 0, i 430, i 425, i 0x0014)'
  Push "$PLUGINSDIR\finish.bmp"
  Call AddBackground
  Call AddWindowControls
  Call StartDragTimer
  GetDlgItem $MainButton $HWNDPARENT 1
  SendMessage $MainButton ${WM_SETTEXT} 0 "STR:Открыть  →"
  System::Call 'user32::SetWindowPos(p $MainButton, p 0, i 108, i 339, i 215, i 49, i 0x0014)'
  SetCtlColors $MainButton 0xFFFFFF 0x17324D
  ShowWindow $MainButton ${SW_SHOW}
  EnableWindow $MainButton 1
  nsDialogs::Show
  nsDialogs::KillTimer $DragTimerProc
FunctionEnd

Function FinishLeave
  ${If} $Installed = 1
    Exec '"$INSTDIR\Soulu.exe"'
    Quit
  ${EndIf}
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
