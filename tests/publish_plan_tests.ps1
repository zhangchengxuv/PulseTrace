# Read-only publication-plan contract tests. No network calls, credentials or tags.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$script=Join-Path $root 'tools\publish-dual.ps1'
$fixture=Join-Path $root ('build\publish-plan-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
'{"version":"0.0.0-test"}' | Set-Content -LiteralPath (Join-Path $fixture 'version.json') -Encoding ASCII
$names=@('PulseTrace-v0.0.0-test-x64.zip','PulseTrace-Setup-v0.0.0-test-x64.exe')
foreach($name in $names){
 $path=Join-Path $fixture $name
 'Controlled publication-plan fixture, never a runnable package.' | Set-Content -LiteralPath $path -Encoding ASCII
 "$((Get-FileHash -LiteralPath $path).Hash.ToLower())  $name" | Set-Content -LiteralPath ($path+'.sha256') -Encoding ASCII
}
$checks=0
function Check($Condition,[string]$Message){$script:checks++;if(-not $Condition){throw $Message}}
$before=@(git show-ref) -join "`n"
$both=(& $script -BuildDirectory $fixture -ReleaseDirectory $fixture | ConvertFrom-Json)
Check $both.DryRun 'Default invocation must be read-only'
Check (@($both.Targets).Count -eq 2) 'Default plan lost a source'
Check (@($both.Assets).Count -eq 4) 'Expected binary packages and checksums'
foreach($source in @('github','gitee')){
 $plan=(& $script -BuildDirectory $fixture -ReleaseDirectory $fixture -Sources $source | ConvertFrom-Json)
 Check (@($plan.Targets).Count -eq 1) 'Selected-source retry must not target the other source'
 Check ($plan.Targets[0].Name -eq $source) 'Wrong publication source'
 Check (@($plan.Assets | Where-Object {$_.Size -gt 0 -and $_.Sha256.Length -eq 64}).Count -eq 4) 'Custom directory assets were not validated'
}
$caught=$false
try {& $script -BuildDirectory $fixture -ReleaseDirectory (Join-Path $fixture 'missing') | Out-Null}catch{$caught=$_.Exception.Message -like 'Missing package:*'}
Check $caught 'Missing custom assets must not fall back to a stale default package'
'invalid checksum' | Set-Content -LiteralPath (Join-Path $fixture ($names[0]+'.sha256')) -Encoding ASCII
$caught=$false
try {& $script -BuildDirectory $fixture -ReleaseDirectory $fixture | Out-Null}catch{$caught=$_.Exception.Message -like 'Checksum sidecar mismatch:*'}
Check $caught 'Corrupt checksum must stop planning'
Check ((@(git show-ref) -join "`n") -eq $before) 'Dry run changed Git refs'
Write-Output "$checks publication-plan checks passed; fixtures: $fixture"
