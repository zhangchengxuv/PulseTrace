param([string]$BuildDirectory='build\llvm-release',[string]$Compiler='build\tools\innosetup-7.1.0\tools\ISCC.exe',[string]$OutputDirectory='build\release',[string]$SetupAppId='')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root $BuildDirectory
$version=Get-Content -LiteralPath (Join-Path $build 'version.json') -Raw | ConvertFrom-Json
if($version.configuration -ne 'Release' -or $version.architecture -ne 'x64'){throw 'Only x64 Release artifacts can be packaged'}
$stage=Join-Path $root "build\release\PulseTrace-v$($version.version)-x64"
$output=if([IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory}else{Join-Path $root $OutputDirectory}
$iscc=if([IO.Path]::IsPathRooted($Compiler)){$Compiler}else{Join-Path $root $Compiler}
if(-not(Test-Path -LiteralPath $iscc)){throw 'Inno Setup compiler missing; run tools/bootstrap-inno.ps1'}
$sig=Get-AuthenticodeSignature -LiteralPath $iscc
if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Thumbprint -ne 'E0AB19C8D38CBF9C44709925122A7A02F8C70CB7'){throw 'Unrecognized Inno Setup compiler signature'}
$arguments=@("/DAppVersion=$($version.version)","/DBaseVersion=$($version.base_version)","/DStageDir=$stage","/DOutputDir=$output","/DSourceRoot=$root")
if($SetupAppId){
 if($SetupAppId -notmatch '^\{[0-9A-Fa-f-]{36}\}$'){throw 'Invalid installer AppId for isolated validation'}
 # Inno requires a doubled opening brace for a literal GUID in AppId.
 $arguments+="/DSetupAppId=$('{{'+$SetupAppId.Substring(1))"
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
& $iscc @arguments (Join-Path $root 'installer\PulseTrace.iss')
if($LASTEXITCODE -ne 0){throw 'Inno Setup compilation failed'}
$file=Join-Path $output "PulseTrace-Setup-v$($version.version)-x64.exe"
"$((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLower())  $([IO.Path]::GetFileName($file))" | Set-Content -LiteralPath ($file+'.sha256') -Encoding ASCII
Write-Output $file
