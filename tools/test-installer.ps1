param([string]$BuildDirectory='build\llvm-release',[ValidateSet('english','chinesesimp')][string]$Language='english',[string]$InstallerPath='')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root $BuildDirectory
$v=Get-Content -LiteralPath (Join-Path $build 'version.json') -Raw | ConvertFrom-Json
$setup=if($InstallerPath){(Resolve-Path -LiteralPath $InstallerPath).Path}else{Join-Path $root "build\release\PulseTrace-Setup-v$($v.version)-x64.exe"}
$key='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{A9F4B181-98CE-426B-AE31-7112540CB549}_is1'
if(Test-Path -LiteralPath $key){throw 'An existing Inno installation is registered; isolated test refuses to replace its registration'}
$fixture=Join-Path $root ('build\installer-validation-'+[Guid]::NewGuid().ToString('N'))
$app=Join-Path $fixture 'Installed App'
$data=Join-Path $app 'data'
$portable=Join-Path $fixture 'Portable Data'
$group='PulseTrace Validation '+[Guid]::NewGuid().ToString('N')
New-Item -ItemType Directory -Force -Path $portable | Out-Null
& (Join-Path $build 'upgrade_fixture.exe') --create $portable
if($LASTEXITCODE -ne 0){throw 'Cannot create controlled legacy fixture'}
$original=(Get-FileHash -LiteralPath (Join-Path $portable 'monitor.db')).Hash
$logs=@();$report=@{Version=$v.version;Root=$fixture;Install=$false;PortableSourcePreserved=$false;UpgradePreservesData=$false;UninstallPreservesData=$false}
try {
 $arguments=@('/CURRENTUSER','/NOLEGACYDETECT',('/LANG='+$Language),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/TASKS=',('/DIR="'+$app+'"'),('/PORTABLESOURCE="'+$portable+'"'),('/GROUP="'+$group+'"'),('/LOG="'+(Join-Path $fixture 'install.log')+'"'))
 $p=Start-Process -FilePath $setup -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait
 if($p.ExitCode -ne 0){throw "Installer failed: $($p.ExitCode); $fixture"}
 if(-not(Test-Path -LiteralPath (Join-Path $app 'installed.ini')) -or -not(Test-Path -LiteralPath (Join-Path $data 'monitor.db'))){throw 'Installed marker or imported database missing'}
 $report.Install=$true
 $probe=Join-Path $data 'permission-test.tmp';[IO.File]::WriteAllText($probe,'writable');Remove-Item -LiteralPath $probe
 $denied=$false;try{$f=[IO.File]::Open((Join-Path $app 'PulseTrace.exe'),[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite);$f.Dispose()}catch [UnauthorizedAccessException]{$denied=$true}
 if(-not $denied){throw 'Program EXE unexpectedly writable by the ordinary test user'}
 $report.DataWritableProgramReadOnly=$true
 $p=Start-Process -FilePath (Join-Path $app 'PulseTrace.exe') -ArgumentList '--self-test','--self-test-page','settings' -WindowStyle Hidden -PassThru -Wait
 if($p.ExitCode -ne 0){throw "Installed WebView self-test failed: $($p.ExitCode)"}
 $config=Get-Content -LiteralPath (Join-Path $data 'config.json') -Raw | ConvertFrom-Json
 if($config.unknown_extension.keep -ne 17 -or $config.retention_days -ne 90){throw 'User configuration lost'}
 $report.PortableSourcePreserved=(Get-FileHash -LiteralPath (Join-Path $portable 'monitor.db')).Hash -eq $original
 if(-not $report.PortableSourcePreserved){throw 'Portable source changed'}
 $report.InstalledWebViewSelftest=$p.ExitCode
 & (Join-Path $build 'upgrade_fixture.exe') --verify $data
 if($LASTEXITCODE -ne 0){throw 'Legacy statistics/monthly values did not survive migration and startup'}
 $before=@{}
 foreach($file in @('config.json','network-months\calendar.json')){$before[$file]=(Get-FileHash -LiteralPath (Join-Path $data $file)).Hash}
 $archives=@(Get-ChildItem -LiteralPath (Join-Path $data 'network-months') -Filter '????-??.json' | ForEach-Object {$j=Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json;@{Name=$_.Name;Upload=$j.upload_bytes;Download=$j.download_bytes}})
 $background=Start-Process -FilePath (Join-Path $app 'PulseTrace.exe') -ArgumentList '--background' -WindowStyle Hidden -PassThru
 Start-Sleep -Seconds 2
 if($background.HasExited){throw 'Isolated background instance exited before upgrade'}
 $p=Start-Process -FilePath $setup -ArgumentList '/CURRENTUSER','/NOLEGACYDETECT',('/LANG='+$Language),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/DIR="'+$app+'"'),('/GROUP="'+$group+'"'),('/LOG="'+(Join-Path $fixture 'upgrade.log')+'"') -WindowStyle Hidden -PassThru -Wait
 if($p.ExitCode -ne 0){throw "Upgrade/reinstall failed: $($p.ExitCode)"}
 if(-not $background.WaitForExit(10000) -or $background.ExitCode -ne 0){throw 'Upgrade did not wait for graceful application exit'}
 $report.RunningUpgradeGracefulExit=$true
 & (Join-Path $build 'upgrade_fixture.exe') --verify $data
 if($LASTEXITCODE -ne 0){throw 'Running upgrade changed historical statistics/monthly totals'}
 foreach($a in $archives){$j=Get-Content -LiteralPath (Join-Path (Join-Path $data 'network-months') $a.Name) -Raw | ConvertFrom-Json;if($j.upload_bytes -ne $a.Upload -or $j.download_bytes -ne $a.Download){throw 'Monthly totals changed during running upgrade'}}
 foreach($file in $before.Keys){if((Get-FileHash -LiteralPath (Join-Path $data $file)).Hash -ne $before[$file]){throw "Upgrade changed data: $file"}}
 $report.UpgradePreservesData=$true
 $afterExit=@{};foreach($f in Get-ChildItem -LiteralPath $data -Recurse -File | Where-Object {$_.Extension -in @('.db','.sqlite3','.json') -and $_.FullName -notlike '*\webview2\*' -and $_.FullName -notlike '*\logs\*'}){$afterExit[$f.FullName]=(Get-FileHash -LiteralPath $f.FullName).Hash}
 $p=Start-Process -FilePath (Join-Path $app 'unins000.exe') -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="'+(Join-Path $fixture 'uninstall.log')+'"') -WindowStyle Hidden -PassThru -Wait
 if($p.ExitCode -ne 0){throw "Uninstall failed: $($p.ExitCode)"}
 foreach($file in $afterExit.Keys){if((Get-FileHash -LiteralPath $file).Hash -ne $afterExit[$file]){throw "Uninstall changed data: $file"}}
 if(Test-Path -LiteralPath (Join-Path $app 'PulseTrace.exe')){throw 'Uninstaller left program EXE'}
 $report.UninstallPreservesData=$true
 $p=Start-Process -FilePath $setup -ArgumentList '/CURRENTUSER','/NOLEGACYDETECT',('/LANG='+$Language),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/DIR="'+$app+'"'),('/GROUP="'+$group+'"') -WindowStyle Hidden -PassThru -Wait
 if($p.ExitCode -ne 0){throw 'Reinstall after retained-data uninstall failed'}
 & (Join-Path $build 'upgrade_fixture.exe') --verify $data
 if($LASTEXITCODE -ne 0){throw 'Reinstall lost retained history'}
 $p=Start-Process -FilePath (Join-Path $app 'unins000.exe') -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -WindowStyle Hidden -PassThru -Wait
 if($p.ExitCode -ne 0){throw 'Final test uninstall failed'}
 $report.ReinstallRestoresData=$true
 $report.InstallerLanguage=$Language
 $report.Success=$true
} finally {
 if(-not $report.Success -and (Test-Path -LiteralPath (Join-Path $app 'unins000.exe'))){
  # Only uninstall the instance created under this unique workspace fixture.
  $expected=[IO.Path]::GetFullPath($fixture)+'\'
  if(-not ([IO.Path]::GetFullPath($app)).StartsWith($expected,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe test cleanup path'}
  $cleanup=Start-Process -FilePath (Join-Path $app 'unins000.exe') -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -WindowStyle Hidden -PassThru -Wait
  $report.TestRegistrationCleanupExit=$cleanup.ExitCode
 }
 $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $fixture 'result.json') -Encoding UTF8
 Write-Output "Installer validation: $fixture"
}
