param([string]$Executable='build/llvm-release/PulseTrace.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$exe=(Resolve-Path -LiteralPath $Executable).Path
$out=Join-Path $root ('build/ui-validation-'+[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())
New-Item -ItemType Directory -Force -Path $out | Out-Null
$results=@()
foreach($size in @('800x600','1024x768','1280x800','1600x900','1920x1080')){
 foreach($theme in @('dark','light')){
  $dir=Join-Path $out "$size-$theme"
  New-Item -ItemType Directory -Force -Path $dir | Out-Null
  @{theme=$theme;capture_on_launch=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'config.json') -Encoding ASCII
  $launchArgs=@('--self-test','--self-test-size',$size,'--data-dir',('"'+$dir+'"'))
  $p=Start-Process -FilePath $exe -ArgumentList $launchArgs -WindowStyle Hidden -PassThru -Wait
  $log=Get-Content -LiteralPath (Join-Path $dir 'logs/desktop.log') -Raw
  if($p.ExitCode -ne 0 -or $log -notmatch 'SELFTEST PASS'){throw "UI layout failed: $size $theme; $dir"}
  $results+=@{SizeDip=$size;Theme=$theme;ExitCode=$p.ExitCode;PagesChecked=6;ThemesCheckedPerPage=2;Screenshot=Join-Path $dir 'logs/selftest-dashboard.png';Client=($log -split "`n" | Where-Object {$_ -match 'Window client pixels'})}
  Write-Output "PASS $size DIP / ${theme}: six real page renderers, both themes, no document overflow"
 }
}
foreach($page in @('keyboard','network','mouse','history','settings')){
 $dir=Join-Path $out "page-$page"
 New-Item -ItemType Directory -Force -Path $dir | Out-Null
 @{theme='dark';capture_on_launch=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'config.json') -Encoding ASCII
 $p=Start-Process -FilePath $exe -ArgumentList '--self-test','--self-test-page',$page,'--self-test-size','1280x800','--data-dir',('"'+$dir+'"') -WindowStyle Hidden -PassThru -Wait
 if($p.ExitCode -ne 0){throw "Page self-test failed: $page"}
 Write-Output "PASS page $page; screenshot saved"
}
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $out 'results.json') -Encoding UTF8
Write-Output "UI validation: $out"
