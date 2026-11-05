param([switch]$Push,[string]$ExpectedCommit='')
# Read-only by default. Ordinary source synchronization never creates a Release/tag.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
function Invoke-Git([string[]]$Arguments){
 $prior=$ErrorActionPreference;$ErrorActionPreference='Continue'
 try{$lines=@(& git @Arguments 2>&1);$code=$LASTEXITCODE;return @{Code=$code;Lines=@($lines | ForEach-Object {$_.ToString()})}}finally{$ErrorActionPreference=$prior}
}
function Required([string[]]$Arguments){$r=Invoke-Git $Arguments;if($r.Code -ne 0){throw ('Git command failed: '+$Arguments[0])};return ($r.Lines -join "`n").Trim()}
function HostName([string]$Url){
 if($Url -match '^git@([^:]+):'){return $Matches[1].ToLower()}
 $uri=[Uri]$Url
 if($uri.Scheme -notin @('https','ssh') -or $uri.UserInfo){throw 'Review remote transport/embedded credentials before synchronization'}
 return $uri.Host.ToLower()
}
function FailureKind([string]$Text){
 if($Text -match 'protected branch|pre-receive hook declined|GH006|GH013'){return 'branch protection rejected the push'}
 if($Text -match 'Authentication|authenticat|credential|terminal prompts|403|401|Access denied|Permission denied|could not read Username|not have.*access'){return 'authentication or write permission unavailable'}
 if($Text -match 'non-fast-forward|fetch first|rejected'){return 'remote history changed or push was rejected; manual review required'}
 if($Text -match 'timed out|Could not resolve|Failed to connect|Connection.*reset|SSL|TLS|502|503|504|remote end hung up'){return 'transient network failure'}
 return 'Git operation failed; inspect local Git diagnostics without exposing credentials'
}
$oldPrompt=$env:GIT_TERMINAL_PROMPT;$oldGcm=$env:GCM_INTERACTIVE
Push-Location -LiteralPath $root
try {
 $env:GIT_TERMINAL_PROMPT='0';$env:GCM_INTERACTIVE='Never'
 $branch=Required @('symbolic-ref','--quiet','--short','HEAD')
 $head=Required @('rev-parse','HEAD')
 if($ExpectedCommit -and $ExpectedCommit -ne $head){throw 'HEAD differs from the explicitly reviewed commit'}
 $ExpectedCommit=$head
 $sources=@{}
 foreach($remote in ((Required @('remote')) -split '\r?\n')){
  $fetch=Required @('remote','get-url',$remote)
  $pushUrls=Required @('remote','get-url','--push','--all',$remote)
  if($pushUrls -ne $fetch){throw 'Separate/multiple push destinations require explicit review'}
  $hostName=HostName $fetch
  if($hostName -in @('github.com','gitee.com')){
   if($sources.ContainsKey($hostName)){throw ('Multiple configured remotes for '+$hostName+'; select the intended repository manually')}
   $sources[$hostName]=$remote
  }
 }
 $results=@()
 foreach($hostName in @('github.com','gitee.com')){
  $row=[ordered]@{Source=$hostName;Remote=$sources[$hostName];Branch=$branch;LocalCommit=$ExpectedCommit;Push=if($Push){'not attempted'}else{'read-only'};RemoteCommit=$null;Verified=$false;Error=$null}
  try {
   if(-not $sources.ContainsKey($hostName)){throw 'Required repository remote is not configured'}
   $remote=$sources[$hostName];$ref='refs/heads/'+$branch
   $advertised=Invoke-Git @('ls-remote','--exit-code',$remote,$ref)
   if($advertised.Code -notin @(0,2)){throw (FailureKind ($advertised.Lines -join "`n"))}
   $before=if($advertised.Code -eq 0){(($advertised.Lines | Select-Object -First 1) -split '\s+')[0]}else{''}
   if($Push){
    if($before -and $before -ne $ExpectedCommit){
     $fetched=Invoke-Git @('fetch','--no-tags',$remote,$ref)
     if($fetched.Code -ne 0){throw (FailureKind ($fetched.Lines -join "`n"))}
     $ancestor=Invoke-Git @('merge-base','--is-ancestor','FETCH_HEAD',$ExpectedCommit)
     if($ancestor.Code -ne 0){throw 'Remote has additional or divergent history; no merge/rebase/force push was performed'}
    }
    if($before -eq $ExpectedCommit){$row.Push='already up to date'}else{
     for($attempt=1;$attempt -le 2;$attempt++){
      $sent=Invoke-Git @('push','--porcelain',$remote,($ExpectedCommit+':'+$ref))
      if($sent.Code -eq 0){$row.Push='success';break}
      $kind=FailureKind ($sent.Lines -join "`n")
      if($kind -ne 'transient network failure' -or $attempt -eq 2){$row.Push='failed';throw $kind}
      Start-Sleep -Seconds 2
     }
    }
   }
   $actual=Invoke-Git @('ls-remote','--exit-code',$remote,$ref)
   if($actual.Code -eq 2){$row.Error='Remote branch does not exist'}elseif($actual.Code -ne 0){throw (FailureKind ($actual.Lines -join "`n"))}else{
    $row.RemoteCommit=(($actual.Lines | Select-Object -First 1) -split '\s+')[0]
    $row.Verified=$row.RemoteCommit -eq $ExpectedCommit
   }
  }catch{$row.Error=$_.Exception.Message}
  $results+=[pscustomobject]$row
 }
 $unchanged=(Required @('rev-parse','HEAD')) -eq $ExpectedCommit
 $report=[ordered]@{Commit=$ExpectedCommit;Branch=$branch;ReadOnly=(-not $Push);LocalHeadUnchanged=$unchanged;Synchronized=($unchanged -and @($results | Where-Object {-not $_.Verified}).Count -eq 0);Repositories=$results}
 $report | ConvertTo-Json -Depth 6
 if($Push -and -not $report.Synchronized){throw 'Dual-repository synchronization incomplete. Successful pushes and local commits were preserved.'}
} finally {$env:GIT_TERMINAL_PROMPT=$oldPrompt;$env:GCM_INTERACTIVE=$oldGcm;Pop-Location}
