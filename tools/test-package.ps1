param([string]$Zip='', [string]$BuildDirectory='build\llvm-release')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$v=Get-Content -LiteralPath (Join-Path (Join-Path $root $BuildDirectory) 'version.json') -Raw | ConvertFrom-Json
if(-not $Zip){$Zip="build\release\PulseTrace-v$($v.version)-x64.zip"}
$zipPath=(Resolve-Path -LiteralPath (Join-Path $root $Zip)).Path
$work=Join-Path $root ('build\package-check-'+[Guid]::NewGuid().ToString('N'))
$extracted=Join-Path $work 'extracted with spaces'
Expand-Archive -LiteralPath $zipPath -DestinationPath $extracted
$count=0
foreach($line in Get-Content -LiteralPath (Join-Path $extracted 'SHA256SUMS.txt')){
 if($line -notmatch '^([a-f0-9]{64})  (.+)$'){throw 'Invalid checksum manifest'}
 $hash=$Matches[1];$relative=$Matches[2];$path=[IO.Path]::GetFullPath((Join-Path $extracted $relative))
 if(-not $path.StartsWith($extracted+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Manifest escapes package'}
 if((Get-FileHash -LiteralPath $path).Hash.ToLower() -ne $hash){throw "Checksum mismatch: $relative"};$count++
}
$files=@(Get-ChildItem -LiteralPath $extracted -Recurse -File)
if($files.Count -ne $count+1){throw 'Unlisted files in package'}
if($files | Where-Object {$_.Extension -in @('.db','.sqlite3','.log','.etl','.pcap','.pcapng') -or $_.Name -match 'tests|probe|benchmark|installed.ini'}){throw 'Unexpected test/runtime/installed files in portable package'}
foreach($name in @('PulseTrace.exe','PulseTraceWidget.exe','PulseTraceMaintenance.exe','WebView2Loader.dll')){if(-not(Test-Path -LiteralPath (Join-Path $extracted $name))){throw "Missing runtime file: $name"}}
$metadata=Get-Content -LiteralPath (Join-Path $extracted 'version.json') -Raw | ConvertFrom-Json
if($metadata.version -ne $v.version -or (Get-Item -LiteralPath (Join-Path $extracted 'PulseTrace.exe')).VersionInfo.ProductVersion -ne $v.version){throw 'Portable version metadata mismatch'}
$p=Start-Process -FilePath (Join-Path $extracted 'PulseTrace.exe') -ArgumentList '--self-test','--self-test-page','settings','--self-test-size','800x600' -WindowStyle Hidden -PassThru -Wait
if($p.ExitCode -ne 0){throw "Portable WebView/settings layout test failed: $($p.ExitCode); $work"}
& (Join-Path $PSScriptRoot 'test-icons.ps1') -Executable (Join-Path $extracted 'PulseTrace.exe')
$report=@{Version=$v.version;ManifestFiles=$count;ZipSha256=(Get-FileHash -LiteralPath $zipPath).Hash.ToLower();NoRuntimeData=$true;ExtractedSelftestExit=$p.ExitCode;SettingsSizeDip='800x600';Work=$work;Success=$true}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root "build\release\package-validation-v$($v.version).json") -Encoding UTF8
$report | ConvertTo-Json -Depth 5