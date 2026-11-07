# Offline regression of the production finalizer: no credentials, Git writes or network calls.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'tools\publish-dual.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Publisher parse error'}
$readerDefinition=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Read-ReleaseJson'},$true)
if(-not $readerDefinition){throw 'Production UTF-8 reader not found'}
. ([ScriptBlock]::Create($readerDefinition.Extent.Text))
$script:unicodeNotes=([string][char]0x4e2d)+[char]0x6587+' / Release notes'
function Invoke-WebRequest {
 param($Uri,$Headers,[switch]$UseBasicParsing)
 $bytes=[Text.Encoding]::UTF8.GetBytes((@{body=$script:unicodeNotes}|ConvertTo-Json))
 [pscustomobject]@{RawContentStream=[IO.MemoryStream]::new($bytes);Content=[Text.Encoding]::GetEncoding(28591).GetString($bytes)}
}
if((Read-ReleaseJson 'https://example.invalid/utf8' @{}).body -ne $script:unicodeNotes){throw 'Charset-free JSON must decode as UTF-8'}
# The finalizer fault matrix below keeps its controlled in-memory API transport.
function Read-ReleaseJson {param($Uri,$Headers) Invoke-RestMethod $Uri -Headers $Headers}
$definition=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Confirm-GiteeRelease'},$true)
if(-not $definition){throw 'Production finalizer not found'}
. ([ScriptBlock]::Create($definition.Extent.Text))
$checks=1
function Check($ok,$message){$script:checks++;if(-not $ok){throw $message}}
function Reset([bool]$state){
 $script:remote=[pscustomobject]@{tag_name='v1.2.3';name='PulseTrace v1.2.3';body=$script:unicodeNotes;target_commitish=('a'*40);prerelease=$state}
 $script:reads=0;$script:writes=0;$script:ignoreWrite=$false;$script:failWrite=$false
}
function Invoke-RestMethod {
 param($Uri,$Method='Get',$Body,$Headers,$ContentType)
 if($Uri -ne 'https://example.invalid/releases/12'){throw 'Unexpected test endpoint'}
 if($Headers.Authorization -ne 'token fixture-token'){throw 'Gitee requests must authenticate via header'}
 if($Method -eq 'Patch'){
  if($ContentType -ne 'application/json; charset=utf-8' -or $Body -isnot [byte[]]){throw 'Release updates must send explicit UTF-8 JSON bytes'}
  $decoded=[Text.Encoding]::UTF8.GetString($Body)|ConvertFrom-Json
  if($decoded.body -ne $script:unicodeNotes -or $decoded.tag_name -ne $script:remote.tag_name -or $decoded.name -ne $script:remote.name){throw 'UTF-8 release update changed notes or identity'}
  $script:writes++
  if($script:failWrite){throw 'Controlled HTTP 406'}
  if(-not $script:ignoreWrite){$script:remote.prerelease=$decoded.prerelease}
  return $script:remote
 }
 $script:reads++;return $script:remote
}
function Confirm([bool]$wanted){Confirm-GiteeRelease -Api 'https://example.invalid' -ReleaseId 12 -ExpectedTag 'v1.2.3' -ExpectedCommit ('a'*40) -Prerelease $wanted -Token 'fixture-token'}
foreach($flag in @($true,$false)){
 Reset $flag;$result=Confirm $flag
 Check ($result.prerelease -eq $flag -and $reads -eq 1 -and $writes -eq 0) 'Already-correct releases must never receive a redundant PATCH'
}
Reset $true;$result=Confirm $false
Check ($result.prerelease -eq $false -and $writes -eq 1 -and $reads -eq 2) 'Required state change must be verified by a fresh read'
foreach($fault in @('tag','commit','type')){
 Reset $true
 switch($fault){tag {$remote.tag_name='v9.9.9'} commit {$remote.target_commitish=('b'*40)} type {$remote.prerelease='true'}}
 $caught=$false;try{Confirm $false | Out-Null}catch{$caught=$true}
 Check ($caught -and $writes -eq 0) 'Unverifiable release must not be modified'
}
Reset $true;$script:failWrite=$true;$caught=$false
try{Confirm $false | Out-Null}catch{$caught=$_.Exception.Message -eq 'Controlled HTTP 406'}
Check ($caught -and $writes -eq 1) 'A necessary state write failure must remain visible'
Reset $true;$script:ignoreWrite=$true;$caught=$false
try{Confirm $false | Out-Null}catch{$caught=$_.Exception.Message -eq 'Gitee release final state verification failed'}
Check ($caught -and $reads -eq 2) 'Acknowledged but ineffective writes must not be reported successful'
Reset $true;$caught=$false
try{Confirm-GiteeRelease -Api 'https://example.invalid' -ReleaseId 12 -ExpectedTag 'v1.2.3' -ExpectedCommit ('a'*40) -Prerelease $false -Token '' | Out-Null}catch{$caught=$true}
Check ($caught -and $writes -eq 0) 'Authentication is required before a necessary write'
Write-Output "$checks release-finalization checks passed; no network calls or Git mutations"
