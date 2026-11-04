param([string]$BuildDirectory='build\llvm-release')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root $BuildDirectory
$release=Join-Path $root 'build\release\PulseTrace-0.2.0-rc3-win-x64'
$stage=Join-Path $root ('build\package-staging-'+[Guid]::NewGuid().ToString('N'))
$zip=Join-Path (Split-Path $release -Parent) 'PulseTrace-0.2.0-rc3-win-x64.zip'
if((Get-Process PulseTrace -ErrorAction SilentlyContinue) | Where-Object {$_.Path -eq (Join-Path $release 'PulseTrace.exe')}){throw 'Exit this release copy before packaging.'}
New-Item -ItemType Directory -Force -Path $stage,(Join-Path $stage 'web'),(Join-Path $stage 'licenses'),(Join-Path $stage 'tools\diagnostics'),(Join-Path $stage 'docs') | Out-Null
try {
    foreach($file in @('PulseTrace.exe','WebView2Loader.dll')){Copy-Item -LiteralPath (Join-Path $build $file) -Destination $stage}
    Copy-Item -LiteralPath (Join-Path $root 'web\desktop') -Destination (Join-Path $stage 'web') -Recurse
    Copy-Item -LiteralPath (Join-Path $root 'docs\desktop-guide.md') -Destination (Join-Path $stage 'README.md')
    Copy-Item -LiteralPath (Join-Path $root 'THIRD-PARTY-NOTICES.md') -Destination $stage
    foreach($name in @('development-status.md','desktop-validation-results.md','ui-design.md','ui-validation-results.md','ansi-arctic-network-validation.md','monthly-traffic-and-caption.md','immersive-dashboard-validation.md')){
        Copy-Item -LiteralPath (Join-Path $root "docs\$name") -Destination (Join-Path $stage 'docs')
    }
    Copy-Item -LiteralPath (Join-Path $root 'tools\install.ps1') -Destination (Join-Path $stage 'tools')
    foreach($name in @('network_capability_probe.exe','network_etw_probe.exe','physical_network_probe.exe','local_echo_validation.exe')){
        Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $stage 'tools\diagnostics')
    }
    Copy-Item -LiteralPath (Join-Path $root 'docs\network-diagnostics.md') -Destination (Join-Path $stage 'tools\diagnostics\README.md')
    $sdk=Join-Path $root 'build\deps\webview2'
    Copy-Item -LiteralPath (Join-Path $sdk 'LICENSE.txt') -Destination (Join-Path $stage 'licenses\WebView2-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $sdk 'NOTICE.txt') -Destination (Join-Path $stage 'licenses\WebView2-NOTICE.txt')
    Copy-Item -LiteralPath (Join-Path $root 'build\tools\llvm-mingw-20260616-ucrt-x86_64\LICENSE.TXT') -Destination (Join-Path $stage 'licenses\LLVM-LICENSE.TXT')
    $jsonLicense=Join-Path $root 'build\deps\json-LICENSE.MIT'
    if(-not(Test-Path -LiteralPath $jsonLicense)){Invoke-WebRequest -UseBasicParsing 'https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT' -OutFile $jsonLicense}
    Copy-Item -LiteralPath $jsonLicense -Destination (Join-Path $stage 'licenses\json-LICENSE.MIT')
    $files=@(Get-ChildItem -LiteralPath $stage -File -Recurse)
    if($files | Where-Object {$_.Extension -in @('.db','.sqlite3','.log','.etl','.pcap','.pcapng')}){throw 'Runtime data found in staging'}
    $manifest=foreach($file in $files | Sort-Object FullName){
        $relative=$file.FullName.Substring($stage.Length+1).Replace('\','/')
        "$((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLower())  $relative"
    }
    $manifest | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII
    $entries=@('PulseTrace.exe','WebView2Loader.dll','README.md','THIRD-PARTY-NOTICES.md','web','licenses','tools','docs','SHA256SUMS.txt')
    New-Item -ItemType Directory -Force -Path $release | Out-Null
    # Construct the ZIP from a fresh stage so stale prototype files and user data cannot enter it.
    Compress-Archive -LiteralPath @($entries | ForEach-Object {Join-Path $stage $_}) -DestinationPath $zip -Force
    foreach($entry in $entries){Copy-Item -LiteralPath (Join-Path $stage $entry) -Destination $release -Recurse -Force}
    $obsolete=Join-Path $release 'web\desktop\app.js'
    if(Test-Path -LiteralPath $obsolete){Remove-Item -LiteralPath $obsolete -Force}
    $hash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower()
    "$hash  $([IO.Path]::GetFileName($zip))" | Set-Content -LiteralPath ($zip+'.sha256') -Encoding ASCII
    Write-Output "Release folder: $release"
    Write-Output "Candidate ZIP: $zip"
} finally {
    $resolved=[IO.Path]::GetFullPath($stage)
    $buildRoot=[IO.Path]::GetFullPath((Join-Path $root 'build'))+'\'
    if(-not $resolved.StartsWith($buildRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Staging cleanup path outside workspace build directory'}
    if(Test-Path -LiteralPath $resolved){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
