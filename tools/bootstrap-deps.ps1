$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$root=Split-Path -Parent $PSScriptRoot
$deps=Join-Path $root 'build\deps'
New-Item -ItemType Directory -Force -Path $deps | Out-Null
function Get-VerifiedFile([string]$Url,[string]$Target,[string]$Hash) {
    if(-not (Test-Path -LiteralPath $Target) -or (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash -ne $Hash) {
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Target
    }
    if((Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash -ne $Hash){throw "SHA256 mismatch: $Target"}
}
Get-VerifiedFile 'https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4314.35/microsoft.web.webview2.1.0.4314.35.nupkg' (Join-Path $deps 'webview2.zip') '90778DEB1B77E36292828B4B5833C9255C27533F40E0DDD55891AE718944EDC4'
Get-VerifiedFile 'https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp' (Join-Path $deps 'json.hpp') 'AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63'
Get-VerifiedFile 'https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip' (Join-Path $deps 'sqlite-3540100.zip') '0A9C8E34352DAD46006AA82836D5383BFF9DC6BC9CE5A60553C05AA30E6AD1F9'
Expand-Archive -LiteralPath (Join-Path $deps 'webview2.zip') -DestinationPath (Join-Path $deps 'webview2') -Force
Expand-Archive -LiteralPath (Join-Path $deps 'sqlite-3540100.zip') -DestinationPath $deps -Force
Write-Output 'Pinned official dependencies verified and extracted to build/deps. No system settings changed.'
& (Join-Path $PSScriptRoot 'bootstrap-music.ps1')
