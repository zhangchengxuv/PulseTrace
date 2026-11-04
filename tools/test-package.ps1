param([string]$Zip='build\release\PulseTrace-0.2.0-rc3-win-x64.zip')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$zipPath=(Resolve-Path -LiteralPath (Join-Path $root $Zip)).Path
$work=Join-Path $root ('build\package-check-'+[Guid]::NewGuid().ToString('N'))
$extracted=Join-Path $work 'extracted'
$installed=Join-Path $work 'install with spaces\PulseTrace'
$passed=$false
try {
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extracted
    $count=0
    foreach($line in Get-Content -LiteralPath (Join-Path $extracted 'SHA256SUMS.txt')){
        if($line -notmatch '^([a-f0-9]{64})  (.+)$'){throw 'Invalid checksum manifest'}
        $hash=$Matches[1];$relative=$Matches[2]
        $path=[IO.Path]::GetFullPath((Join-Path $extracted $relative))
        if(-not $path.StartsWith($extracted+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Manifest path outside extracted package'}
        if((Get-FileHash -LiteralPath $path).Hash.ToLower() -ne $hash){throw "Checksum mismatch: $relative"}
        $count++
    }
    if(Test-Path -LiteralPath (Join-Path $extracted 'web\desktop\app.js')){throw 'Obsolete desktop prototype included'}
    if(Get-ChildItem -LiteralPath $extracted -Recurse | Where-Object {$_.Name -eq 'data' -or $_.Extension -in @('.db','.sqlite3','.log','.etl','.pcap','.pcapng')}){throw 'Runtime data included in package'}
    $p=Start-Process -FilePath (Join-Path $extracted 'PulseTrace.exe') -ArgumentList '--self-test','--self-test-page','network','--self-test-size','800x600' -WindowStyle Hidden -Wait -PassThru
    if($p.ExitCode -ne 0){throw "Extracted application self-test failed: $($p.ExitCode)"}
    New-Item -ItemType Directory -Force -Path (Join-Path $installed 'data') | Out-Null
    @{theme='light';capture_on_launch=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $installed 'data\config.json') -Encoding ASCII
    'installation preservation fixture' | Set-Content -LiteralPath (Join-Path $installed 'data\keep.txt') -Encoding ASCII
    $before=(Get-FileHash -LiteralPath (Join-Path $installed 'data\config.json')).Hash
    & (Join-Path $extracted 'tools\install.ps1') -Destination $installed -NoShortcut
    if((Get-FileHash -LiteralPath (Join-Path $installed 'data\config.json')).Hash -ne $before -or -not(Test-Path -LiteralPath (Join-Path $installed 'data\keep.txt'))){throw 'Installation overwrote data'}
    $p=Start-Process -FilePath (Join-Path $installed 'PulseTrace.exe') -ArgumentList '--self-test','--self-test-page','settings','--self-test-size','1024x768' -WindowStyle Hidden -Wait -PassThru
    if($p.ExitCode -ne 0){throw "Installed application self-test failed: $($p.ExitCode)"}
    & (Join-Path $PSScriptRoot 'test-icons.ps1') -Executable (Join-Path $extracted 'PulseTrace.exe')
    $report=[ordered]@{Version='0.2.0-rc3';RecordedUtc=[DateTime]::UtcNow.ToString('o');ManifestFiles=$count;ZipSha256=(Get-FileHash -LiteralPath $zipPath).Hash;ExeSha256=(Get-FileHash -LiteralPath (Join-Path $extracted 'PulseTrace.exe')).Hash;NoRuntimeData=$true;ObsoletePrototypeAbsent=$true;ExtractedSelftestExit=0;InstalledSelftestExit=0;InstallPreservesDataAndConfiguration=$true;IconSizes=@(16,24,32,48,64,128,256)}
    $report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $root 'build\release\package-validation-rc3.json') -Encoding UTF8
    $report | ConvertTo-Json -Depth 4
    $passed=$true
} finally {
    $resolved=[IO.Path]::GetFullPath($work)
    $buildRoot=[IO.Path]::GetFullPath((Join-Path $root 'build'))+'\'
    if(-not $resolved.StartsWith($buildRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Test cleanup target outside build directory'}
    if($passed -and (Test-Path -LiteralPath $resolved)){Remove-Item -LiteralPath $resolved -Recurse -Force}
    elseif(Test-Path -LiteralPath $resolved){Write-Output "Failed package test diagnostics retained at $resolved"}
}
