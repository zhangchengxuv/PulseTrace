param([string]$BuildDirectory='build\llvm-release')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root $BuildDirectory
$release=Join-Path $root 'build\release\PulseTrace-0.2.0-rc1-win-x64'
New-Item -ItemType Directory -Force -Path $release,(Join-Path $release 'web'),(Join-Path $release 'licenses'),(Join-Path $release 'tools'),(Join-Path $release 'docs') | Out-Null
foreach($file in @('PulseTrace.exe','WebView2Loader.dll')){Copy-Item -LiteralPath (Join-Path $build $file) -Destination $release -Force}
Copy-Item -LiteralPath (Join-Path $root 'web\desktop') -Destination (Join-Path $release 'web') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root 'docs\desktop-guide.md') -Destination (Join-Path $release 'README.md') -Force
Copy-Item -LiteralPath (Join-Path $root 'THIRD-PARTY-NOTICES.md') -Destination $release -Force
foreach($name in @('development-status.md','desktop-validation-results.md')){if(Test-Path -LiteralPath (Join-Path $root "docs\$name")){Copy-Item -LiteralPath (Join-Path $root "docs\$name") -Destination (Join-Path $release 'docs') -Force}}
Copy-Item -LiteralPath (Join-Path $root 'tools\install.ps1') -Destination (Join-Path $release 'tools') -Force
$sdk=Join-Path $root 'build\deps\webview2'
Copy-Item -LiteralPath (Join-Path $sdk 'LICENSE.txt') -Destination (Join-Path $release 'licenses\WebView2-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $sdk 'NOTICE.txt') -Destination (Join-Path $release 'licenses\WebView2-NOTICE.txt') -Force
Copy-Item -LiteralPath (Join-Path $root 'build\tools\llvm-mingw-20260616-ucrt-x86_64\LICENSE.TXT') -Destination (Join-Path $release 'licenses\LLVM-LICENSE.TXT') -Force
$jsonLicense=Join-Path $root 'build\deps\json-LICENSE.MIT'
if(-not (Test-Path -LiteralPath $jsonLicense)){Invoke-WebRequest -UseBasicParsing 'https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT' -OutFile $jsonLicense}
Copy-Item -LiteralPath $jsonLicense -Destination (Join-Path $release 'licenses\json-LICENSE.MIT') -Force
$allowed=@('PulseTrace.exe','WebView2Loader.dll','README.md','THIRD-PARTY-NOTICES.md','web','licenses','tools','docs')
$files=@();foreach($entry in $allowed){$path=Join-Path $release $entry;if(Test-Path -LiteralPath $path -PathType Leaf){$files+=Get-Item -LiteralPath $path}else{$files+=Get-ChildItem -LiteralPath $path -File -Recurse}}
$manifest=foreach($file in $files | Sort-Object FullName){$relative=$file.FullName.Substring($release.Length+1).Replace('\','/');"$((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLower())  $relative"}
$manifest | Set-Content -LiteralPath (Join-Path $release 'SHA256SUMS.txt') -Encoding ASCII
$zip=Join-Path (Split-Path $release -Parent) 'PulseTrace-0.2.0-rc1-win-x64.zip'
# Explicit allowlist: never package data/, logs, databases, user configuration,
# screenshots, dependency caches, or test fixtures, even after a deployment test.
$paths=@($allowed | ForEach-Object {Join-Path $release $_})+(Join-Path $release 'SHA256SUMS.txt')
Compress-Archive -LiteralPath $paths -DestinationPath $zip -Force
Write-Output "Release folder: $release"
Write-Output "Candidate ZIP: $zip"
