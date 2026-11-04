param(
    [string]$Executable=(Join-Path (Split-Path -Parent $PSScriptRoot) 'build\llvm-release\PulseTrace.exe'),
    [int]$Cycles=2,
    [string]$DataDirectory='',
    [ValidateSet('UI','Core','Window')][string]$Mode='UI',
    [ValidateRange(5,86400)][int]$CoreSeconds=30
)
$ErrorActionPreference='Stop'
$exe=(Resolve-Path -LiteralPath $Executable).Path
$directory=Split-Path -Parent $exe
$arguments=if($Mode -eq 'UI'){@('--self-test','--cycles',([string]$Cycles))}elseif($Mode -eq 'Core'){@('--background','--core-seconds',([string]$CoreSeconds))}else{@('--core-seconds',([string]$CoreSeconds),'--self-test-early-close')}
# --core-seconds implies background in older releases. The existing early-close/reopen
# diagnostic opens the normal page on the first timer tick, without --self-test work.
if($DataDirectory){$arguments+=@('--data-dir',('"'+[IO.Path]::GetFullPath($DataDirectory)+'"'))}
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
$started=[DateTime]::UtcNow
$samples=New-Object 'System.Collections.Generic.List[object]'
$known=@{}
while(-not $process.HasExited){
    $all=@(Get-CimInstance Win32_Process)
    $ids=New-Object 'System.Collections.Generic.HashSet[int]'
    [void]$ids.Add($process.Id)
    $birth=@{}; $birth[$process.Id]=$process.StartTime
    do{$changed=$false;foreach($item in $all){if($ids.Contains([int]$item.ParentProcessId) -and -not $ids.Contains([int]$item.ProcessId) -and $item.CreationDate -ge $birth[[int]$item.ParentProcessId]){[void]$ids.Add([int]$item.ProcessId);$birth[[int]$item.ProcessId]=$item.CreationDate;$changed=$true}}}while($changed)
    $members=@()
    foreach($item in $all){
        if(-not $ids.Contains([int]$item.ProcessId)){continue}
        try{$p=Get-Process -Id $item.ProcessId -ErrorAction Stop
            $members += [pscustomobject]@{Pid=$p.Id;Name=$p.ProcessName;CreationUtc=$item.CreationDate.ToUniversalTime().ToString('o');PrivateBytes=$p.PrivateMemorySize64;WorkingSet=$p.WorkingSet64;CpuSeconds=$p.TotalProcessorTime.TotalSeconds;Threads=$p.Threads.Count;Handles=$p.HandleCount;IoWriteTransferBytes=[long]$item.WriteTransferCount}
            if($p.Id -ne $process.Id){$known[$p.Id]=$item.CreationDate}
        }catch{}
    }
    $samples.Add([pscustomobject]@{ElapsedSeconds=([DateTime]::UtcNow-$started).TotalSeconds;Processes=$members})
    Start-Sleep -Milliseconds 500
    $process.Refresh()
}
$process.WaitForExit()
$remaining=@()
foreach($id in $known.Keys){$current=Get-CimInstance Win32_Process -Filter "ProcessId=$id" -ErrorAction SilentlyContinue;if($current -and $current.CreationDate -eq $known[$id]){$remaining+=$id}}
$report=[pscustomobject]@{Mode=$Mode;ExecutableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;ExitCode=$process.ExitCode;Cycles=$Cycles;MainPid=$process.Id;RemainingKnownChildPids=$remaining;Note='Working Set sums include shared pages; process IO writes include IPC and are not physical disk IO. 500ms sampling can miss brief child processes. Measurement script overhead is external to app. Core mode uses paused capture by default.';Samples=$samples}
$out=Join-Path $directory ('data\resource-test-'+[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()+'.json')
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $out -Encoding UTF8
Write-Output "Resource report: $out"
Write-Output "ExitCode=$($process.ExitCode); Remaining observed child processes=$($remaining.Count)"
if($process.ExitCode -ne 0 -or $remaining.Count -ne 0){throw 'Lifecycle test failed or child processes remain; inspect report'}
if($Mode -eq 'Window' -and $known.Count -eq 0){throw 'Window measurement did not observe any WebView2 child process'}
