# Extracts a pinned compiler ZIP into build/tools; never runs an installer or changes the registry.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$package=Join-Path $root 'build\deps\tools.innosetup.7.1.0.nupkg'
$directory=Join-Path $root 'build\tools\innosetup-7.1.0'
$expected='AAD15C662593C6F5656457A5C001F4591BABD72820CACF63F9706D4AF5D93B75'
if(-not(Test-Path -LiteralPath $package)){New-Item -ItemType Directory -Force -Path (Split-Path $package) | Out-Null;Invoke-WebRequest -UseBasicParsing 'https://api.nuget.org/v3-flatcontainer/tools.innosetup/7.1.0/tools.innosetup.7.1.0.nupkg' -OutFile $package}
if((Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash -ne $expected){throw 'Compiler package checksum mismatch'}
if(-not(Test-Path -LiteralPath $directory)){Add-Type -AssemblyName System.IO.Compression.FileSystem;[IO.Compression.ZipFile]::ExtractToDirectory($package,$directory)}
foreach($f in Get-ChildItem -LiteralPath (Join-Path $directory 'tools') -File | Where-Object {$_.Extension -in @('.exe','.dll')}){
 $sig=Get-AuthenticodeSignature -LiteralPath $f.FullName
 if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Thumbprint -ne 'E0AB19C8D38CBF9C44709925122A7A02F8C70CB7'){throw "Compiler signature mismatch: $($f.Name)"}
}
Write-Output "Compiler ready: $directory\tools\ISCC.exe"
