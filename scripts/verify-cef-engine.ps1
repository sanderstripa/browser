param(
  [Parameter(Mandatory=$true)][string]$BuildDir,
  [string]$ReportPath
)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDir).Path
$exe = Join-Path $build 'Soulu.exe'
$dll = Join-Path $build 'libcef.dll'
if (!(Test-Path -LiteralPath $exe) -or !(Test-Path -LiteralPath $dll)) {
  throw 'Soulu.exe or bundled libcef.dll is missing'
}
$probe = Join-Path ([IO.Path]::GetTempPath()) ("soulu-engine-" + [guid]::NewGuid() + ".json")
try {
  $process = Start-Process -FilePath $exe -WorkingDirectory $build -WindowStyle Hidden -PassThru -ArgumentList @("--engine-version-file=`"$probe`"")
  if (!$process.WaitForExit(30000)) {
    $process.Kill()
    throw 'Engine version probe timed out'
  }
  if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $probe)) {
    throw "Engine version probe failed: exit $($process.ExitCode)"
  }
  $version = Get-Content -LiteralPath $probe -Raw | ConvertFrom-Json
  if ($version.cef -cne '154.0.33' -or $version.chromium -cne '154.0.8037.94') {
    throw "Unsupported engine: CEF $($version.cef), Chromium $($version.chromium)"
  }
  $report = [ordered]@{
    cef = $version.cef
    chromium = $version.chromium
    commit = $env:GITHUB_SHA
    run_id = $env:GITHUB_RUN_ID
    soulu_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    libcef_sha256 = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
  }
  $json = $report | ConvertTo-Json
  Write-Host $json
  if ($ReportPath) { $json | Set-Content -LiteralPath $ReportPath -Encoding utf8 }
} finally {
  Remove-Item -LiteralPath $probe -Force -ErrorAction SilentlyContinue
}
