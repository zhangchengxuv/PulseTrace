param([Parameter(Mandatory=$true)][string]$Destination,[switch]$NoShortcut)
# Explicit user-run installation. Never enables monitoring, autostart, or uploads.
$ErrorActionPreference='Stop'
$destinationPath=[IO.Path]::GetFullPath($Destination)
$sourcePath=Split-Path -Parent $PSScriptRoot
if(-not (Test-Path -LiteralPath (Join-Path $sourcePath 'PulseTrace.exe'))){throw 'Run this installer from the extracted release package tools/ folder.'}
if($destinationPath.TrimEnd('\') -eq [IO.Path]::GetPathRoot($destinationPath).TrimEnd('\')){throw 'Choose a dedicated application folder, not a drive root.'}
if((Get-Process PulseTrace -ErrorAction SilentlyContinue) | Where-Object {$_.Path -eq (Join-Path $destinationPath 'PulseTrace.exe')}){throw 'Exit PulseTrace from its tray menu before updating.'}
New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null
foreach($file in @('PulseTrace.exe','WebView2Loader.dll','README.md','THIRD-PARTY-NOTICES.md')){Copy-Item -LiteralPath (Join-Path $sourcePath $file) -Destination $destinationPath -Force}
foreach($folder in @('web','licenses','docs','tools')){Copy-Item -LiteralPath (Join-Path $sourcePath $folder) -Destination $destinationPath -Recurse -Force}
if(-not $NoShortcut){
    $shell=New-Object -ComObject WScript.Shell
    $shortcut=$shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Programs')) 'PulseTrace.lnk'))
    $shortcut.TargetPath=Join-Path $destinationPath 'PulseTrace.exe'
    $shortcut.WorkingDirectory=$destinationPath
    $shortcut.IconLocation=(Join-Path $destinationPath 'PulseTrace.exe')+',0'
    $shortcut.Save()
}
Write-Output "Installed to $destinationPath. No autostart or monitoring enabled."
