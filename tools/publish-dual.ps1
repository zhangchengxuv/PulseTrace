param([string]$BuildDirectory='build\llvm-release',[switch]$Publish,
 [string]$ReleaseDirectory='build\release',
 [ValidateSet('github','gitee')][string[]]$Sources=@('github','gitee'))
# No remote mutation, tag creation, credential lookup, or upload occurs without -Publish.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
function Confirm-GiteeRelease {
 param([string]$Api,[long]$ReleaseId,[string]$ExpectedTag,[string]$ExpectedCommit,[bool]$Prerelease,[string]$Token)
 $uri=$Api+'/releases/'+$ReleaseId
 $headers=@{Authorization=('token '+$Token);'User-Agent'='PulseTrace-publisher'}
 $valid={param($r) $r.tag_name -eq $ExpectedTag -and $r.target_commitish -eq $ExpectedCommit -and $r.prerelease -is [bool]}
 $final=Invoke-RestMethod $uri -Headers $headers
 if(-not (& $valid $final)){throw 'Gitee release identity or state could not be verified'}
 # Creation already uses prerelease=true. Do not issue a redundant write after verified uploads.
 if($final.prerelease -ne $Prerelease){
  if(-not $Token){throw 'Gitee release-state change needs authentication'}
  Invoke-RestMethod -Method Patch $uri -Headers $headers -Body @{prerelease=$Prerelease.ToString().ToLower()} | Out-Null
  $final=Invoke-RestMethod $uri -Headers $headers
 }
 if(-not (& $valid $final) -or $final.prerelease -ne $Prerelease){throw 'Gitee release final state verification failed'}
 return $final
}
Push-Location -LiteralPath $root
try {
 $v=Get-Content -LiteralPath (Join-Path $BuildDirectory 'version.json') -Raw | ConvertFrom-Json
 $tag='v'+$v.version
 $commit=(git rev-parse HEAD).Trim()
 $tree=(git rev-parse 'HEAD^{tree}').Trim()
 $dirty=[bool](git status --porcelain)
 $releaseRoot=if([IO.Path]::IsPathRooted($ReleaseDirectory)){$ReleaseDirectory}else{Join-Path $root $ReleaseDirectory}
 $targets=@(@{Name='github';Remote='PulseTrace';URL='https://github.com/zhangchengxuv/PulseTrace.git';API='https://api.github.com/repos/zhangchengxuv/PulseTrace'},@{Name='gitee';Remote='PulseTrace_gitee';URL='https://gitee.com/zhangchenxuv/pulse-trace.git';API='https://gitee.com/api/v5/repos/zhangchenxuv/pulse-trace'})
 $targets=@($targets | Where-Object {$_.Name -in $Sources})
 foreach($t in $targets){$actual=(git remote get-url $t.Remote).Trim();if($LASTEXITCODE -ne 0 -or $actual -ne $t.URL){throw "Remote differs from the reviewed repository: $($t.Remote)"}}
 $names=@("PulseTrace-v$($v.version)-x64.zip","PulseTrace-v$($v.version)-x64.zip.sha256","PulseTrace-Setup-v$($v.version)-x64.exe","PulseTrace-Setup-v$($v.version)-x64.exe.sha256","SHA256SUMS.txt")
 $assets=foreach($n in $names){$file=Join-Path $releaseRoot $n;if(-not(Test-Path -LiteralPath $file)){throw "Missing package: $n"};@{Name=$n;Path=$file;Size=(Get-Item -LiteralPath $file).Length;Sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLower()}}
 foreach($a in $assets | Where-Object {$_.Name -notlike '*.sha256' -and $_.Name -ne 'SHA256SUMS.txt'}){$line=(Get-Content -LiteralPath ($a.Path+'.sha256') -Raw).Trim();if($line -ne ($a.Sha256+'  '+$a.Name)){throw "Checksum sidecar mismatch: $($a.Name)"}}
 $plan=@{Version=$v.version;Commit=$commit;Tree=$tree;Dirty=$dirty;DryRun=(-not $Publish);Targets=$targets;Assets=@($assets | ForEach-Object {@{Name=$_.Name;Size=$_.Size;Sha256=$_.Sha256}})}
 if(-not $Publish){$plan | ConvertTo-Json -Depth 8;Write-Verbose 'Dry run only. Review, commit, tag, validate the clean source, then invoke -Publish explicitly.';return}
 if($dirty){throw 'Commit reviewed source changes before explicit publication'}
 $tagCommit=git rev-parse ($tag+'^{commit}') 2>$null
 if($LASTEXITCODE -ne 0 -or $tagCommit.Trim() -ne $commit){throw 'Create a reviewed release tag pointing to HEAD before publishing'}
 $reportPath=Join-Path $releaseRoot "validation-$tag.json"
 $report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
 if($report.SourceCommit -ne $commit -or $report.SourceTree -ne $tree -or -not $report.ReleaseTestsPassed -or -not $report.DebugTestsPassed -or -not $report.InstallerTestsPassed){throw 'Validation report must bind this clean source and all required tests'}
 foreach($a in $assets){if($report.Artifacts.($a.Name) -ne $a.Sha256){throw "Validated artifact changed: $($a.Name)"}}
 $gh=$env:GH_TOKEN;if(-not $gh){$gh=$env:GITHUB_TOKEN};$gt=$env:GITEE_TOKEN
 $notesFile=Join-Path $root "docs\releases\$tag.md"
 $notes=[IO.File]::ReadAllText($notesFile,[Text.Encoding]::UTF8)
 $results=@();Add-Type -AssemblyName System.Net.Http
 foreach($t in $targets){
  $receipt=@{Source=$t.Name;Commit=$commit;Tag=$tag;Success=$false;Assets=@()}
  try {
   if(($t.Name -eq 'github' -and -not $gh) -or ($t.Name -eq 'gitee' -and -not $gt)){throw ('Missing '+$t.Name+' release API credentials; other sources remain independent')}
   & git push $t.Remote 'HEAD:refs/heads/main' ('refs/tags/'+$tag+':refs/tags/'+$tag)
   if($LASTEXITCODE -ne 0){throw 'Non-forced Git push failed; history was not overwritten'}
   $heads=git ls-remote $t.Remote refs/heads/main ('refs/tags/'+$tag) ('refs/tags/'+$tag+'^{}')
   if($LASTEXITCODE -ne 0 -or -not($heads -match ('^'+$commit+'\s+refs/heads/main$')) -or -not($heads -match ('^'+$commit+'\s+refs/tags/'+[regex]::Escape($tag)+'(\^\{\})?$'))){throw 'Remote branch/tag commit verification failed'}
   if($t.Name -eq 'github'){
    $headers=@{Authorization='Bearer '+$gh;Accept='application/vnd.github+json';'User-Agent'='PulseTrace-publisher';'X-GitHub-Api-Version'='2022-11-28'}
    $releases=Invoke-RestMethod ($t.API+'/releases?per_page=100') -Headers $headers
    $release=$releases | Where-Object {$_.tag_name -eq $tag} | Select-Object -First 1
    if($release -and -not $release.draft){throw 'Published release is immutable; use a new version'}
    if(-not $release){$payload=@{tag_name=$tag;target_commitish=$commit;name="PulseTrace $tag";body=$notes;draft=$true;prerelease=($v.version -match '-')} | ConvertTo-Json;$release=Invoke-RestMethod -Method Post ($t.API+'/releases') -Headers $headers -ContentType 'application/json' -Body ([Text.Encoding]::UTF8.GetBytes($payload))}
    foreach($a in $assets){
     $existing=$release.assets | Where-Object {$_.name -eq $a.Name} | Select-Object -First 1
     if($existing){if($existing.digest -ne ('sha256:'+$a.Sha256)){throw 'Existing draft attachment differs'}}else{
      $url=($release.upload_url -replace '\{.*$','')+'?name='+[Uri]::EscapeDataString($a.Name)
      if(([Uri]$url).Host -ne 'uploads.github.com'){throw 'Unexpected GitHub upload host'}
      $existing=Invoke-RestMethod -Method Post $url -Headers $headers -ContentType 'application/octet-stream' -InFile $a.Path
     }
     if($existing.size -ne $a.Size -or $existing.digest -ne ('sha256:'+$a.Sha256)){throw 'GitHub attachment verification failed'}
     $receipt.Assets+=@{Name=$a.Name;Sha256=$a.Sha256;Size=$a.Size}
    }
    $payload=@{draft=$false;prerelease=($v.version -match '-');make_latest='false'} | ConvertTo-Json
    $final=Invoke-RestMethod -Method Patch ($t.API+'/releases/'+$release.id) -Headers $headers -ContentType 'application/json' -Body $payload
    $receipt.URL=$final.html_url
   }else{
    $giteeHeaders=@{Authorization=('token '+$gt);'User-Agent'='PulseTrace-publisher'}
    $releases=Invoke-RestMethod ($t.API+'/releases?per_page=100') -Headers $giteeHeaders
    if($releases | Where-Object {$_.tag_name -eq $tag}){throw 'Existing Gitee release must be reviewed manually; assets are never overwritten'}
    $release=Invoke-RestMethod -Method Post ($t.API+'/releases') -Headers $giteeHeaders -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes((@{tag_name=$tag;target_commitish=$commit;name="PulseTrace $tag";body=$notes;prerelease='true'}|ConvertTo-Json)))
    foreach($a in $assets){
     $client=[Net.Http.HttpClient]::new();$client.Timeout=[TimeSpan]::FromMinutes(5)
     $client.DefaultRequestHeaders.Authorization=[Net.Http.Headers.AuthenticationHeaderValue]::new('token',$gt)
     $multipart=[Net.Http.MultipartFormDataContent]::new();$stream=[IO.File]::OpenRead($a.Path)
     # Authenticate via header to avoid public API rate-limit buckets and keep tokens out of URLs.
     try{$multipart.Add([Net.Http.StreamContent]::new($stream),'file',$a.Name);$upload=$t.API+'/releases/'+$release.id+'/attach_files';$response=$client.PostAsync($upload,$multipart).GetAwaiter().GetResult();if(-not $response.IsSuccessStatusCode){throw "Gitee attachment upload HTTP $([int]$response.StatusCode)"}}finally{$multipart.Dispose();$stream.Dispose();$client.Dispose();$upload=$null}
     $files=Invoke-RestMethod ($t.API+'/releases/'+$release.id+'/attach_files?per_page=100') -Headers $giteeHeaders
     $remote=$files | Where-Object {$_.name -eq $a.Name} | Select-Object -First 1
     if(-not $remote -or $remote.size -ne $a.Size){throw 'Gitee attachment size verification failed'}
     $uri=[Uri]$remote.browser_download_url
     if($uri.Scheme -ne 'https' -or $uri.Host -ne 'gitee.com' -or -not $uri.AbsolutePath.StartsWith('/zhangchenxuv/pulse-trace/releases/')){throw 'Untrusted Gitee attachment download URL'}
     $verify=Join-Path $env:TEMP ('PulseTrace-publish-verify-'+[Guid]::NewGuid().ToString('N'))
     try{Invoke-WebRequest -UseBasicParsing $uri.AbsoluteUri -OutFile $verify;if((Get-FileHash -LiteralPath $verify -Algorithm SHA256).Hash.ToLower() -ne $a.Sha256){throw 'Gitee uploaded attachment hash mismatch'}}finally{if(Test-Path -LiteralPath $verify){Remove-Item -LiteralPath $verify -Force}}
     $receipt.Assets+=@{Name=$a.Name;Sha256=$a.Sha256;Size=$a.Size}
    }
    Confirm-GiteeRelease -Api $t.API -ReleaseId $release.id -ExpectedTag $tag -ExpectedCommit $commit -Prerelease ($v.version -match '-') -Token $gt | Out-Null
    $receipt.URL='https://gitee.com/zhangchenxuv/pulse-trace/releases/tag/'+$tag
   }
   $receipt.Success=$true
  }catch{$message=$_.Exception.Message;foreach($secret in @($gh,$gt)){if($secret){$message=$message.Replace($secret,'[redacted]')}};$receipt.Error=$message}
  $results+=$receipt
 }
 $receiptName='publish-'+$tag+'-'+($Sources -join '-')+'.json'
 $results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $releaseRoot $receiptName) -Encoding UTF8
 $results | ConvertTo-Json -Depth 8
 if($results | Where-Object {-not $_.Success}){throw 'Publication incomplete; inspect per-source receipt. No force push or asset replacement was attempted.'}
} finally {$gh=$null;$gt=$null;Pop-Location}
