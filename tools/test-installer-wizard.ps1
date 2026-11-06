param([Parameter(Mandatory=$true)][string]$InstallerPath,[string]$BuildDirectory='build\llvm-release')
# Real interactive Inno windows, operated by the isolated native test process.
# Installation and data upgrades are tested separately by test-installer.ps1.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$setup=(Resolve-Path -LiteralPath $InstallerPath).Path
$probe=Join-Path $root "$BuildDirectory\installer_wizard_probe.exe"
# This test only opens, navigates and cancels private installer processes. It never
# installs or unregisters software, so an existing production installation is safe.
$fixture=Join-Path $root ('build\installer-wizard-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$report=@{InstallerSha256=(Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLower();Success=$false;Cases=@()}
try {
 foreach($language in @('english','chinesesimp')){
  foreach($mode in @('cancel-welcome','navigate')){
   $evidence=Join-Path $fixture ($language+'-'+$mode)
   & $probe $setup $evidence $language $mode
   if($LASTEXITCODE -ne 0){throw "Wizard regression failed: $language/$mode; $evidence"}
   $report.Cases+=@{Language=$language;Mode=$mode;Passed=$true;Evidence=$evidence}
  }
 }
 $report.Success=$true
} finally {
 $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $fixture 'result.json') -Encoding UTF8
 Write-Output "Wizard validation: $fixture"
}
