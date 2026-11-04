param(
    [string]$Repository='zhangchengxuv/PulseTrace',
    [string]$Tag='v0.2.0-rc3',
    [switch]$Publish
)
# Explicit user-run publishing only. Authentication stays in process memory.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $root
$headers=$null;$credential=$null
try {
    if($Repository -ne 'zhangchengxuv/PulseTrace' -or $Tag -ne 'v0.2.0-rc3'){throw 'Review repository, version and asset allowlist before publishing another release.'}
    if(git status --porcelain){throw 'Commit the reviewed source changes before publishing.'}
    $commit=(git rev-parse HEAD).Trim()
    if($LASTEXITCODE -ne 0){throw 'Cannot resolve source commit'}
    $tagCommit=(git rev-parse ($Tag+'^{commit}')).Trim()
    if($LASTEXITCODE -ne 0 -or $tagCommit -ne $commit){throw 'Release tag must point to HEAD'}
    $remote=git ls-remote PulseTrace refs/heads/main
    if($LASTEXITCODE -ne 0 -or -not($remote -match ('^'+$commit+'\s'))){throw 'Push the exact reviewed HEAD to the configured main branch first.'}
    $remoteTag=git ls-remote PulseTrace ('refs/tags/'+$Tag+'^{}')
    if($LASTEXITCODE -ne 0 -or -not($remoteTag -match ('^'+$commit+'\s'))){throw 'Push the annotated tag pointing to the reviewed HEAD first.'}
    $base=Join-Path $root 'build\release'
    $validation=Get-Content -LiteralPath (Join-Path $base 'release-validation-rc3.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $tree=(git rev-parse 'HEAD^{tree}').Trim()
    if($LASTEXITCODE -ne 0 -or $validation.Version -ne '0.2.0-rc3' -or $validation.SourceCommit -ne $commit -or $validation.SourceTree -ne $tree -or $validation.UncommittedWorkingTree -ne $false){throw 'Validation report must bind the clean, tagged source commit and tree.'}
    if($validation.ReleaseFullCTest -ne '7/7' -or $validation.DebugFullCTest -ne '7/7' -or $validation.Package.NoRuntimeData -ne $true -or $validation.Package.ExtractedSelftestExit -ne 0 -or $validation.Package.InstalledSelftestExit -ne 0 -or $validation.Package.InstallPreservesDataAndConfiguration -ne $true){throw 'Required tests and isolated package validation are missing.'}
    $zip=Join-Path $base 'PulseTrace-0.2.0-rc3-win-x64.zip'
    $zipHash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower()
    $sidecar=(Get-Content -LiteralPath ($zip+'.sha256') -Raw).Trim()
    if($validation.Package.ZipSha256.ToLower() -ne $zipHash -or $sidecar -ne ($zipHash+'  '+[IO.Path]::GetFileName($zip))){throw 'ZIP or checksum sidecar differs from the validated package.'}
    $token=$env:GH_TOKEN
    if(-not $token){$token=$env:GITHUB_TOKEN}
    if(-not $token){
        $env:GIT_TERMINAL_PROMPT='0'
        $lines="protocol=https`nhost=github.com`n`n" | git -c credential.interactive=false credential fill 2>$null
        if($LASTEXITCODE -ne 0){throw 'No GitHub credentials available in Git credential helper'}
        $credential=@{}
        foreach($line in $lines){$split=$line.IndexOf('=');if($split -gt 0){$credential[$line.Substring(0,$split)]=$line.Substring($split+1)}}
        $token=$credential['password'];$lines=$null
    }
    if(-not $token){throw 'No GitHub authentication available'}
    $headers=@{Authorization='Bearer '+$token;Accept='application/vnd.github+json';'X-GitHub-Api-Version'='2022-11-28';'User-Agent'='PulseTrace-release'}
    $token=$null
    $api='https://api.github.com/repos/'+$Repository
    $bodyFile=Join-Path $root ('docs\releases\'+$Tag+'.md')
    $body=[IO.File]::ReadAllText($bodyFile,[Text.Encoding]::UTF8)
    $payload=@{tag_name=$Tag;target_commitish=$commit;name=('PulseTrace '+$Tag+' (Windows x64)');body=$body;draft=$true;prerelease=$true;make_latest='false'}
    $existing=Invoke-RestMethod -Uri ($api+'/releases?per_page=100') -Headers $headers
    $release=$existing | Where-Object {$_.tag_name -eq $Tag} | Select-Object -First 1
    if($release -and -not $release.draft){throw 'This version is already published; do not overwrite its assets.'}
    if(-not $release){$release=Invoke-RestMethod -Method Post -Uri ($api+'/releases') -Headers $headers -ContentType 'application/json' -Body ([Text.Encoding]::UTF8.GetBytes(($payload | ConvertTo-Json -Depth 5)))}
    $assetNames=@('PulseTrace-0.2.0-rc3-win-x64.zip','PulseTrace-0.2.0-rc3-win-x64.zip.sha256','release-validation-rc3.json')
    foreach($name in $assetNames){
        $file=Join-Path $base $name
        if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw "Missing release asset: $name"}
        $local=Get-FileHash -LiteralPath $file -Algorithm SHA256
        $asset=$release.assets | Where-Object {$_.name -eq $name} | Select-Object -First 1
        if($asset){
            if($asset.digest -ne ('sha256:'+$local.Hash.ToLower())){throw "Existing draft asset differs: $name"}
        }else{
            $upload=($release.upload_url -replace '\{.*$','')+'?name='+[Uri]::EscapeDataString($name)
            if(([Uri]$upload).Host -ne 'uploads.github.com' -or ([Uri]$upload).Scheme -ne 'https'){throw 'Unexpected asset upload destination'}
            $type=if($name.EndsWith('.zip')){'application/zip'}elseif($name.EndsWith('.json')){'application/json'}else{'text/plain'}
            $asset=Invoke-RestMethod -Method Post -Uri $upload -Headers $headers -ContentType $type -InFile $file
        }
        if($asset.state -ne 'uploaded' -or $asset.size -ne (Get-Item -LiteralPath $file).Length){throw "Upload incomplete: $name"}
        if($asset.digest -and $asset.digest -ne ('sha256:'+$local.Hash.ToLower())){throw "Uploaded digest mismatch: $name"}
        Write-Output "Verified asset: $name"
    }
    if($Publish){
        $update=@{draft=$false;prerelease=$true;make_latest='false'} | ConvertTo-Json
        $release=Invoke-RestMethod -Method Patch -Uri ($api+'/releases/'+$release.id) -Headers $headers -ContentType 'application/json' -Body ([Text.Encoding]::UTF8.GetBytes($update))
    }
    $final=Invoke-RestMethod -Uri ($api+'/releases/'+$release.id) -Headers $headers
    if($Publish -and ($final.draft -or -not $final.prerelease)){throw 'Release publication did not reach the expected state'}
    $receipt=@{Url=$final.html_url;Tag=$Tag;Commit=$commit;Draft=$final.draft;Prerelease=$final.prerelease;Assets=@($final.assets | Select-Object name,size,digest,browser_download_url)}
    $receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $base 'github-release-rc3.json') -Encoding UTF8
    $receipt | ConvertTo-Json -Depth 6
} finally {
    if($headers){$headers['Authorization']=$null};if($credential){$credential.Clear()};$token=$null
    Pop-Location
}
