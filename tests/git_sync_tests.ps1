# Isolated fake-Git contract tests: no network, real refs, or credentials are used.
$ErrorActionPreference='Stop'
$sync=Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\sync-remotes.ps1'
$checks=0
function Check($Value,[string]$Message){$script:checks++;if(-not $Value){throw $Message}}
function git {
 $a=@($args);$c=$global:PulseTraceGitTest;$c.Calls.Add(($a -join ' '));$global:LASTEXITCODE=0
 switch($a[0]){
  'symbolic-ref' {return 'main'}
  'rev-parse' {return $c.Head}
  'remote' {
   if($a.Count -eq 1){return @('custom_gh','custom_gt')}
   if($a[-1] -eq 'custom_gh'){return 'https://github.com/example/project.git'}
   return 'https://gitee.com/example/project.git'
  }
  'ls-remote' {
   $r=$c.Repositories[$a[2]]
   if(-not $r.Sha){$global:LASTEXITCODE=2;return}
   return ($r.Sha+"`trefs/heads/main")
  }
  'fetch' {$c.Fetched=$a[2];return}
  'merge-base' {if($c.Diverged -eq $c.Fetched){$global:LASTEXITCODE=1};return}
  'push' {
   $name=$a[2];$r=$c.Repositories[$name];$r.Attempts++
   if($c.AuthFailure -eq $name){$global:LASTEXITCODE=128;return 'fatal: Authentication failed'}
   if($c.NetworkOnce -eq $name -and $r.Attempts -eq 1){$global:LASTEXITCODE=128;return 'fatal: Failed to connect'}
   if($c.AcknowledgeOnly -ne $name){$r.Sha=($a[3] -split ':')[0]};return 'ok'
  }
  default {throw ('Unexpected fake Git call: '+($a -join ' '))}
 }
}
function Run([string]$Scenario,[bool]$Push){
 $global:PulseTraceGitTest=@{Head=('a'*40);Repositories=@{custom_gh=@{Sha=('b'*40);Attempts=0};custom_gt=@{Sha=('b'*40);Attempts=0}};Calls=[Collections.Generic.List[string]]::new();Fetched='';Diverged='';AuthFailure='';NetworkOnce='';AcknowledgeOnly=''}
 $c=$global:PulseTraceGitTest
 switch($Scenario){
  'empty-gitee' {$c.Repositories.custom_gt.Sha=''}
  'auth-gitee' {$c.AuthFailure='custom_gt'}
  'divergence' {$c.Diverged='custom_gt'}
  'network-once' {$c.NetworkOnce='custom_gt'}
  'false-success' {$c.AcknowledgeOnly='custom_gt'}
 }
 $lines=[Collections.Generic.List[string]]::new();$failed=$false
 try{& $sync -Push:$Push -ExpectedCommit $c.Head | ForEach-Object {$lines.Add([string]$_)}}catch{$failed=$true}
 $report=($lines -join "`n") | ConvertFrom-Json
 Check ($report.Repositories[0].Remote -eq 'custom_gh' -and $report.Repositories[1].Remote -eq 'custom_gt') 'Remote discovery assumed origin/gitee'
 Check (-not @($c.Calls | Where-Object {$_ -match '(^| )--force|^rebase|^tag|^reset|^clean'}).Count) 'Unsafe Git operation attempted'
 return @{Context=$c;Report=$report;Failed=$failed}
}
try {
 $r=Run 'readonly' $false;Check (-not $r.Failed -and $r.Report.ReadOnly) 'Read-only inspection failed';Check (-not @($r.Context.Calls | Where-Object {$_ -like 'push *'}).Count) 'Read-only inspection pushed'
 foreach($scenario in @('normal','empty-gitee')){$r=Run $scenario $true;Check (-not $r.Failed -and $r.Report.Synchronized) 'Safe dual push failed';Check ($r.Context.Repositories.custom_gh.Sha -eq $r.Context.Head -and $r.Context.Repositories.custom_gt.Sha -eq $r.Context.Head) 'Different commits pushed'}
 $r=Run 'auth-gitee' $true;Check ($r.Failed -and -not $r.Report.Synchronized) 'Authentication failure hidden';Check ($r.Report.Repositories[0].Verified -and $r.Report.Repositories[1].Push -eq 'failed') 'Partial success not preserved';Check ($r.Context.Repositories.custom_gt.Attempts -eq 1) 'Authentication failure unnecessarily retried'
 $r=Run 'divergence' $true;Check ($r.Failed -and $r.Context.Repositories.custom_gt.Attempts -eq 0) 'Divergent history was pushed';Check $r.Report.Repositories[0].Verified 'Successful first repository was rewritten'
 $r=Run 'network-once' $true;Check (-not $r.Failed -and $r.Report.Synchronized -and $r.Context.Repositories.custom_gt.Attempts -eq 2) 'Bounded transient retry failed'
 $r=Run 'false-success' $true;Check ($r.Failed -and -not $r.Report.Repositories[1].Verified) 'Push exit code mistaken for remote SHA equality'
 Write-Output "$checks Git synchronization contract checks passed; no real Git command was executed"
}finally{Remove-Variable -Scope Global -Name PulseTraceGitTest -ErrorAction SilentlyContinue}
