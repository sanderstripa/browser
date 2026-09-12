[CmdletBinding()]param()
$ErrorActionPreference='Stop'
$installDir=Join-Path $env:LOCALAPPDATA 'VlessXhttpChrome'
$hostDir=Join-Path $installDir 'native-host'
$xrayDir=Join-Path $hostDir 'xray'
$backupDir=Join-Path $hostDir 'xray-backup'
if(-not((Resolve-Path $hostDir).Path.StartsWith((Resolve-Path $installDir).Path,[StringComparison]::OrdinalIgnoreCase))){throw 'Unsafe install path.'}
Write-Host 'Checking the latest official Xray-core release...'
$release=Invoke-RestMethod 'https://api.github.com/repos/XTLS/Xray-core/releases/latest' -Headers @{'User-Agent'='VlessXhttpChromeUpdater/2.1'}
$asset=$release.assets|Where-Object name -eq 'Xray-windows-64.zip'|Select-Object -First 1
if(-not $asset){throw 'Official Windows 64-bit Xray archive was not found.'}
$expected=if($asset.digest -match '^sha256:([a-f0-9]{64})$'){$Matches[1]}else{''}
$digestAsset=$release.assets|Where-Object name -eq 'Xray-windows-64.zip.dgst'|Select-Object -First 1
if(-not $expected -and $digestAsset){$digest=(Invoke-WebRequest $digestAsset.browser_download_url).Content;$expected=[regex]::Match($digest,'(?i)([a-f0-9]{64})').Groups[1].Value}
if(-not $expected){throw 'The release has no published SHA-256 digest.'}
$temp=Join-Path ([IO.Path]::GetTempPath()) ('vless-xray-'+[guid]::NewGuid())
New-Item -ItemType Directory $temp|Out-Null
try{
  $zip=Join-Path $temp 'xray.zip';$new=Join-Path $temp 'new'
  Invoke-WebRequest $asset.browser_download_url -OutFile $zip
  if((Get-FileHash $zip -Algorithm SHA256).Hash -ne $expected){throw 'Xray SHA-256 mismatch.'}
  Expand-Archive $zip $new -Force
  $newExe=Join-Path $new 'xray.exe';if(-not(Test-Path $newExe)){throw 'Downloaded archive has no xray.exe.'}
  & $newExe version|Out-Host;if($LASTEXITCODE -ne 0){throw 'New Xray executable failed its version test.'}
  if(Test-Path $backupDir){Remove-Item -LiteralPath $backupDir -Recurse -Force}
  if(Test-Path $xrayDir){Move-Item -LiteralPath $xrayDir -Destination $backupDir}
  try{Move-Item -LiteralPath $new -Destination $xrayDir}catch{if(Test-Path $backupDir){Move-Item -LiteralPath $backupDir -Destination $xrayDir};throw}
  Write-Host "Updated to $($release.tag_name). Previous version is kept in xray-backup."
}finally{Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue}
Read-Host 'Press Enter to close'
