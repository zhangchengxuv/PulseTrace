# Pinned official header generator and Windows API contracts; no system install.
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$root=Split-Path -Parent $PSScriptRoot
$deps=Join-Path $root 'build/deps'
New-Item -ItemType Directory -Force -Path $deps | Out-Null
foreach($item in @(
 @{Name='cppwinrt';Version='2.0.240405.15';Package='microsoft.windows.cppwinrt';Hash='E889007B5D9235931E7340DDF737D2C346EEBDD23C619F1F4F2426A2AAE47180'},
 @{Name='sdk-contracts';Version='10.0.26100.3916';Package='microsoft.windows.sdk.contracts';Hash='3B30CFCCA11E5C9C36BCEFE5ED79DA651B1C60FA838C18CD1165A4B1B399B63F'})){
 $file=Join-Path $deps ($item.Name+'.zip')
 if(-not(Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file).Hash -ne $item.Hash){Invoke-WebRequest -UseBasicParsing -Uri ("https://api.nuget.org/v3-flatcontainer/{0}/{1}/{0}.{1}.nupkg" -f $item.Package,$item.Version) -OutFile $file}
 if((Get-FileHash -LiteralPath $file).Hash -ne $item.Hash){throw 'Music dependency checksum mismatch'}
 Expand-Archive -LiteralPath $file -DestinationPath (Join-Path $deps $item.Name) -Force
}
& (Join-Path $deps 'cppwinrt/bin/cppwinrt.exe') -input (Join-Path $deps 'sdk-contracts/ref/netstandard2.0') -output (Join-Path $deps 'cppwinrt/include')
if($LASTEXITCODE -ne 0){throw 'C++/WinRT projection generation failed'}
Copy-Item -LiteralPath (Join-Path $deps 'cppwinrt/LICENSE') -Destination (Join-Path $deps 'CppWinRT-LICENSE.txt') -Force
Write-Output 'Pinned C++/WinRT media headers generated.'
