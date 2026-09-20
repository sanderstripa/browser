param([string]$Destination = "$PSScriptRoot\..\vpn\native-host\xray")

$ErrorActionPreference = "Stop"
$release = Invoke-RestMethod -Headers @{"User-Agent"="Soulu-Build"} -Uri "https://api.github.com/repos/XTLS/Xray-core/releases/latest"
$asset = $release.assets | Where-Object { $_.name -eq "Xray-windows-64.zip" } | Select-Object -First 1
if (-not $asset) { throw "Xray-windows-64.zip was not found in the latest release." }

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("soulu-xray-" + [guid]::NewGuid())
$zip = Join-Path $temp $asset.name
$expanded = Join-Path $temp "expanded"
New-Item -ItemType Directory -Path $expanded -Force | Out-Null
New-Item -ItemType Directory -Path $Destination -Force | Out-Null

try {
  Invoke-WebRequest -Headers @{"User-Agent"="Soulu-Build"} -Uri $asset.browser_download_url -OutFile $zip
  if ($asset.digest -and $asset.digest.StartsWith("sha256:")) {
    $expected = $asset.digest.Substring(7).ToLowerInvariant()
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
      $bytes = [System.IO.File]::ReadAllBytes($zip)
      $actual = ([System.BitConverter]::ToString($sha256.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant()
    } finally {
      $sha256.Dispose()
    }
    if ($actual -ne $expected) { throw "Xray archive SHA-256 verification failed." }
  }
  Expand-Archive -Path $zip -DestinationPath $expanded -Force
  foreach ($name in @("xray.exe", "geoip.dat", "geosite.dat", "LICENSE", "README.md")) {
    $source = Join-Path $expanded $name
    if (Test-Path $source) { Copy-Item $source (Join-Path $Destination $name) -Force }
  }
} finally {
  Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
}
