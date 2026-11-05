param([ValidateSet('PortableLLVM','MSVC')][string]$Toolchain='PortableLLVM')
# Explicit local release preparation; this script never commits, pushes, or publishes.
$ErrorActionPreference='Stop'
if($Toolchain -ne 'PortableLLVM'){throw 'The automated installer pipeline currently supports PortableLLVM single-config builds; MSVC packaging requires layout review'}
$root=Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $root
try {
 if(git status --porcelain){throw 'Commit reviewed source first so validation can bind an exact source tree'}
 $commit=(git rev-parse HEAD).Trim();$tree=(git rev-parse 'HEAD^{tree}').Trim()
 & (Join-Path $PSScriptRoot 'build.ps1') -Toolchain $Toolchain -Configuration Release
 & (Join-Path $PSScriptRoot 'build.ps1') -Toolchain $Toolchain -Configuration Debug
 $build=if($Toolchain -eq 'PortableLLVM'){'build\llvm-release'}else{'build\msvc'}
 $v=Get-Content -LiteralPath (Join-Path $build 'version.json') -Raw | ConvertFrom-Json
 & (Join-Path $PSScriptRoot 'bootstrap-inno.ps1')
 & (Join-Path $PSScriptRoot 'package.ps1') -BuildDirectory $build
 & (Join-Path $PSScriptRoot 'build-installer.ps1') -BuildDirectory $build
 & (Join-Path $PSScriptRoot 'test-installer-wizard.ps1') -BuildDirectory $build -InstallerPath (Join-Path $root "build\release\PulseTrace-Setup-v$($v.version)-x64.exe")
 & (Join-Path $PSScriptRoot 'test-installer.ps1') -BuildDirectory $build -Language english
 & (Join-Path $PSScriptRoot 'test-installer.ps1') -BuildDirectory $build -Language chinesesimp
 & (Join-Path $PSScriptRoot 'test-legacy-layout.ps1') -BuildDirectory $build
 & (Join-Path $PSScriptRoot 'test-package.ps1') -BuildDirectory $build
 if((git rev-parse HEAD).Trim() -ne $commit -or (git rev-parse 'HEAD^{tree}').Trim() -ne $tree -or (git status --porcelain)){throw 'Source changed while preparing release'}
 $artifacts=@{}
 foreach($name in @("PulseTrace-v$($v.version)-x64.zip","PulseTrace-v$($v.version)-x64.zip.sha256","PulseTrace-Setup-v$($v.version)-x64.exe","PulseTrace-Setup-v$($v.version)-x64.exe.sha256")){$artifacts[$name]=(Get-FileHash -LiteralPath (Join-Path $root "build\release\$name") -Algorithm SHA256).Hash.ToLower()}
 $result=@{Version=$v.version;SourceCommit=$commit;SourceTree=$tree;UncommittedWorkingTree=$false;ReleaseTestsPassed=$true;DebugTestsPassed=$true;InstallerTestsPassed=$true;Artifacts=$artifacts;RecordedUtc=[DateTime]::UtcNow.ToString('o')}
 $result | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $root "build\release\validation-v$($v.version).json") -Encoding UTF8
 Write-Output 'Local release preparation complete. Publishing remains a separate explicit operation.'
} finally {Pop-Location}
