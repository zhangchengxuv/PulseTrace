param([string]$BuildDirectory='build\llvm-release',[switch]$Publish)
# Compatibility entry point. Default is a strictly read-only publication plan.
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'publish-dual.ps1') -BuildDirectory $BuildDirectory -Publish:$Publish