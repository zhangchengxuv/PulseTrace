param([string]$BuildDirectory='build\llvm-release')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root $BuildDirectory
$v=Get-Content -LiteralPath (Join-Path $build 'version.json') -Raw | ConvertFrom-Json
$setup=Join-Path $root "build\release\PulseTrace-Setup-v$($v.version)-x64.exe"
$key='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{A9F4B181-98CE-426B-AE31-7112540CB549}_is1'
if(Test-Path -LiteralPath $key){throw 'An existing installation is registered; refusing to replace it'}
$reports=@()
foreach($occupied in @($false,$true)){
 $fixture=Join-Path $root ('build\legacy-layout-'+[Guid]::NewGuid().ToString('N'))
 $app=Join-Path $fixture 'Old Installation';$source=Join-Path $fixture 'Legacy LocalAppData';$data=Join-Path $app 'data'
 New-Item -ItemType Directory -Force -Path $app | Out-Null
 foreach($name in @('PulseTrace.exe','PulseTraceWidget.exe','PulseTraceMaintenance.exe','WebView2Loader.dll')){Copy-Item -LiteralPath (Join-Path $root "build\release\PulseTrace-v0.3.0-rc1-x64\$name") -Destination $app}
 Copy-Item -LiteralPath (Join-Path $root 'build\release\PulseTrace-v0.3.0-rc1-x64\web') -Destination $app -Recurse
 & (Join-Path $build 'upgrade_fixture.exe') --create $source
 if($LASTEXITCODE -ne 0){throw 'Fixture creation failed'}
 $cfg=Get-Content -LiteralPath (Join-Path $source 'config.json') -Raw | ConvertFrom-Json
 $cfg | Add-Member -NotePropertyName language -NotePropertyValue 'en-US';$cfg | Add-Member -NotePropertyName update_source -NotePropertyValue 'gitee'
 $cfg | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $source 'config.json') -Encoding ASCII
 if($occupied){& (Join-Path $build 'upgrade_fixture.exe') --create $data;$cfg.theme='light';$cfg | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $data 'config.json') -Encoding ASCII}
 "[PulseTrace]`r`nDataDirectory=$source`r`n" | Set-Content -LiteralPath (Join-Path $app 'installed.ini') -Encoding Unicode
 $report=@{Root=$fixture;TargetInitiallyOccupied=$occupied;Success=$false}
 try {
  $old=Start-Process -FilePath (Join-Path $app 'PulseTrace.exe') -ArgumentList '--background' -WindowStyle Hidden -PassThru
  Start-Sleep -Seconds 2
  if($old.HasExited){throw 'Legacy external-data instance did not start'}
  $p=Start-Process -FilePath $setup -ArgumentList '/CURRENTUSER','/NOLEGACYDETECT','/LANG=chinesesimp','/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/DIR="'+$app+'"'),('/GROUP="PulseTrace Validation '+[Guid]::NewGuid().ToString('N')+'"'),('/LOG="'+(Join-Path $fixture 'install.log')+'"') -WindowStyle Hidden -PassThru -Wait
  if($p.ExitCode -ne 0 -or -not $old.WaitForExit(10000)){throw 'Legacy upgrade failed or old writer was not stopped'}
  & (Join-Path $build 'upgrade_fixture.exe') --verify $data
  if($LASTEXITCODE -ne 0){throw 'Destination history/monthly archive mismatch'}
  & (Join-Path $build 'upgrade_fixture.exe') --verify $source
  if($LASTEXITCODE -ne 0){throw 'Original source history changed'}
  $new=Get-Content -LiteralPath (Join-Path $data 'config.json') -Raw | ConvertFrom-Json
  if($new.language -ne 'en-US' -or $new.update_source -ne 'gitee' -or $new.taskbar_mode -ne 'tray' -or $new.capture_on_launch -ne $false){throw 'Preferences lost during Chinese-language upgrade'}
  if($occupied -and $new.theme -ne 'light'){throw 'Occupied target was silently overwritten by legacy source'}
  $report.SourcePreserved=$true;$report.SettingsPreserved=$true;$report.SafeWriterExit=$true;$report.CoLocatedData=$true
  $report.Success=$true
 } finally {
  if(Test-Path -LiteralPath (Join-Path $app 'unins000.exe')){$p=Start-Process -FilePath (Join-Path $app 'unins000.exe') -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -WindowStyle Hidden -PassThru -Wait;$report.UninstallExit=$p.ExitCode}
  $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $fixture 'result.json') -Encoding UTF8
  $reports+=$report
 }
}
$reports | ConvertTo-Json -Depth 5
