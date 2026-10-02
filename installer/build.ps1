param([Parameter(Mandatory=$true)][string]$BuildDir,[Parameter(Mandatory=$true)][string]$OutFile)
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot '../scripts/verify-cef-engine.ps1') -BuildDir $BuildDir
$payload=Join-Path $PSScriptRoot 'payload.exe'
& "${env:ProgramFiles(x86)}\NSIS\makensis.exe" /INPUTCHARSET UTF8 "/DBUILD_DIR=$BuildDir" "/DOUT_FILE=$payload" "$PSScriptRoot/payload.nsi"
if($LASTEXITCODE -ne 0){throw 'Payload compilation failed'}
$icon=(Join-Path $BuildDir 'ui/soulu-icon.ico').Replace('\','/')
$logo=(Join-Path $BuildDir 'ui/branding/soulu-512.png').Replace('\','/')
@"
100 RCDATA "payload.exe"
101 ICON "$icon"
102 RCDATA "$logo"
"@ | Set-Content -Encoding utf8 (Join-Path $PSScriptRoot 'setup.rc')
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$cmd=Join-Path $PSScriptRoot 'compile.cmd'
@"
call "$vcvars"
cd /d "$PSScriptRoot"
rc /nologo /fo setup.res setup.rc
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++17 /EHsc /O2 /MT setup.cpp setup.res user32.lib gdi32.lib /link /SUBSYSTEM:WINDOWS /out:"$OutFile"
"@ | Set-Content -Encoding ascii $cmd
& cmd /c $cmd
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
