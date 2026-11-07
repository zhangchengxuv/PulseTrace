param([string]$BuildDirectory='build\llvm-release')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root $BuildDirectory
$version=Get-Content -LiteralPath (Join-Path $build 'version.json') -Raw | ConvertFrom-Json
if($version.configuration -ne 'Release' -or $version.architecture -ne 'x64'){throw 'Only x64 Release builds are publishable'}
$release=Join-Path $root "build\release\PulseTrace-v$($version.version)-x64"
$stage=Join-Path $root ('build\package-staging-'+[Guid]::NewGuid().ToString('N'))
$zip=$release+'.zip'
if((Get-Process PulseTrace -ErrorAction SilentlyContinue) | Where-Object {$_.Path -eq (Join-Path $release 'PulseTrace.exe')}){throw 'Exit this release copy before packaging'}
if(Test-Path -LiteralPath (Join-Path $release 'data')){throw 'Release directory contains user data; choose a new version or move the release output manually'}
New-Item -ItemType Directory -Force -Path $stage,(Join-Path $stage 'web'),(Join-Path $stage 'licenses'),(Join-Path $stage 'docs') | Out-Null
try {
 foreach($name in @('PulseTrace.exe','PulseTraceWidget.exe','PulseTraceMaintenance.exe','WebView2Loader.dll')){Copy-Item -LiteralPath (Join-Path $build $name) -Destination $stage}
 $exeVersion=(Get-Item -LiteralPath (Join-Path $stage 'PulseTrace.exe')).VersionInfo.ProductVersion
 if($exeVersion -ne $version.version){throw "EXE metadata mismatch: $exeVersion / $($version.version)"}
 Copy-Item -LiteralPath (Join-Path $root 'web\desktop') -Destination (Join-Path $stage 'web') -Recurse
 Copy-Item -LiteralPath (Join-Path $root 'docs\desktop-guide.md') -Destination (Join-Path $stage 'README.md')
 Copy-Item -LiteralPath (Join-Path $root 'THIRD-PARTY-NOTICES.md') -Destination $stage
 Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $stage
 foreach($name in @('MinGW-w64-runtime-LICENSE.txt','MinGW-w64-LICENSE.txt','winpthreads-LICENSE.txt')){Copy-Item -LiteralPath (Join-Path $root "licenses\$name") -Destination (Join-Path $stage 'licenses')}
 foreach($name in @('app-usage.md','traffic-rankings.md','performance-continuation.md','campus-portal.md','cloud-backup.md')){Copy-Item -LiteralPath (Join-Path $root "docs\$name") -Destination (Join-Path $stage 'docs')}
 Copy-Item -LiteralPath (Join-Path $root "docs\releases\v$($version.version).md") -Destination (Join-Path $stage 'docs\release-notes.md')
 Copy-Item -LiteralPath (Join-Path $build 'version.json') -Destination $stage
 $sdk=Join-Path $root 'build\deps\webview2'
 Copy-Item -LiteralPath (Join-Path $sdk 'LICENSE.txt') -Destination (Join-Path $stage 'licenses\WebView2-LICENSE.txt')
 Copy-Item -LiteralPath (Join-Path $sdk 'NOTICE.txt') -Destination (Join-Path $stage 'licenses\WebView2-NOTICE.txt')
 Copy-Item -LiteralPath (Join-Path $root 'build\tools\llvm-mingw-20260616-ucrt-x86_64\LICENSE.TXT') -Destination (Join-Path $stage 'licenses\LLVM-LICENSE.TXT')
 Copy-Item -LiteralPath (Join-Path $root 'build\deps\json-LICENSE.MIT') -Destination (Join-Path $stage 'licenses\json-LICENSE.MIT')
 Copy-Item -LiteralPath (Join-Path $root 'build\tools\innosetup-7.1.0\tools\license.txt') -Destination (Join-Path $stage 'licenses\InnoSetup-LICENSE.txt')
 $files=@(Get-ChildItem -LiteralPath $stage -File -Recurse)
 if($files | Where-Object {$_.Extension -in @('.db','.sqlite3','.log','.etl','.pcap','.pcapng')}){throw 'Runtime data found in staging'}
 $manifest=foreach($file in $files | Sort-Object FullName){"$((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLower())  $($file.FullName.Substring($stage.Length+1).Replace('\','/'))"}
 $manifest | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII
 New-Item -ItemType Directory -Force -Path $release | Out-Null
 # Replace only known shipped files; never enumerate/delete a user data tree.
 foreach($entry in Get-ChildItem -LiteralPath $stage){Copy-Item -LiteralPath $entry.FullName -Destination $release -Recurse -Force}
 Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
 "$((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower())  $([IO.Path]::GetFileName($zip))" | Set-Content -LiteralPath ($zip+'.sha256') -Encoding ASCII
 Write-Output "Release folder: $release"
 Write-Output "Portable ZIP: $zip"
} finally {
 $resolved=[IO.Path]::GetFullPath($stage)
 $buildRoot=[IO.Path]::GetFullPath((Join-Path $root 'build'))+'\'
 if(-not $resolved.StartsWith($buildRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Cleanup path outside workspace build directory'}
 if(Test-Path -LiteralPath $resolved){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
