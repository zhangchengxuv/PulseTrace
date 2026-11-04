param([switch]$Apply)
# Only disposable browser profiles in completed, explicitly identified UI tests.
# Keep source, dependencies, user data, SQLite fixtures, logs and screenshots.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=[IO.Path]::GetFullPath((Join-Path $root 'build'))
$testRoots=@()
foreach($name in @('immersive-baseline-ui','immersive-scroll-baseline','immersive-ui-after','immersive-real-data','immersive-small','immersive-crash','immersive-cycles20','arctic-keyboard-1793779842814','final-arctic-ui-1793781726736')){
    $testRoots+=Join-Path $build $name
}
foreach($matrix in Get-ChildItem -LiteralPath $build -Directory -Filter 'ui-validation-*'){
    $testRoots+=@(Get-ChildItem -LiteralPath $matrix.FullName -Directory | Select-Object -ExpandProperty FullName)
}
foreach($configuration in @('llvm-release','llvm-debug')){
    $tests=Join-Path $build ($configuration+'\data\tests')
    if(Test-Path -LiteralPath $tests){$testRoots+=@(Get-ChildItem -LiteralPath $tests -Directory | Select-Object -ExpandProperty FullName)}
}
$candidates=@()
foreach($testRoot in $testRoots | Select-Object -Unique){
    $log=Join-Path $testRoot 'logs\desktop.log'
    $profile=Join-Path $testRoot 'webview2'
    if(-not(Test-Path -LiteralPath $profile -PathType Container) -or -not(Test-Path -LiteralPath $log -PathType Leaf)){continue}
    if(-not(Select-String -LiteralPath $log -Pattern 'SELFTEST PASS:' -Quiet)){continue}
    $full=(Resolve-Path -LiteralPath $profile).Path
    if(-not $full.StartsWith($build+'\',[StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $full) -ne 'webview2'){throw 'Unexpected cleanup path'}
    $items=@(Get-Item -LiteralPath $full -Force)+@(Get-ChildItem -LiteralPath $full -Recurse -Force)
    if($items | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw "Reparse point in disposable profile: $full"}
    $bytes=($items | Where-Object {-not $_.PSIsContainer} | Measure-Object Length -Sum).Sum
    $candidates+=[pscustomobject]@{Path=$full;Bytes=[long]$bytes}
}
if($Apply -and (Get-Process -Name PulseTrace -ErrorAction SilentlyContinue)){throw 'A PulseTrace process is running; defer profile cleanup until test processes have exited.'}
$removed=@()
$prefix=if($Apply){'REMOVE '}else{'PLAN '}
foreach($item in $candidates){
    Write-Output ($prefix+$item.Path)
    if($Apply){Remove-Item -LiteralPath $item.Path -Recurse -Force;$removed+=$item}
}
$report=[ordered]@{Applied=[bool]$Apply;Profiles=$candidates.Count;Bytes=[long](($candidates | Measure-Object Bytes -Sum).Sum);Removed=@($removed | ForEach-Object {$_.Path.Substring($root.Length+1)});Kept='All application data and release directories; source; dependencies; test databases, logs and screenshots'}
if($Apply){$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $build 'cleanup-rc3.json') -Encoding UTF8}
$report | ConvertTo-Json -Depth 4
