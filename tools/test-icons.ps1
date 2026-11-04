param([string]$Executable='build/llvm-release/PulseTrace.exe')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class PulseIconCheck {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr LoadLibraryEx(string file,IntPtr h,uint flags);
 [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr module);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern IntPtr LoadImage(IntPtr module,IntPtr name,uint type,int cx,int cy,uint flags);
 [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr icon);
 [DllImport("shell32.dll",CharSet=CharSet.Unicode)] public static extern uint ExtractIconEx(string file,int index,IntPtr large,IntPtr small,uint count);
}
'@
$exe=(Resolve-Path -LiteralPath $Executable).Path
if([PulseIconCheck]::ExtractIconEx($exe,-1,[IntPtr]::Zero,[IntPtr]::Zero,0) -ne 1){throw 'Expected one embedded icon group'}
$module=[PulseIconCheck]::LoadLibraryEx($exe,[IntPtr]::Zero,0x22)
if($module -eq [IntPtr]::Zero){throw 'Cannot inspect executable resources'}
try{
 foreach($size in @(16,24,32,48,64,128,256)){
  $handle=[PulseIconCheck]::LoadImage($module,[IntPtr]101,1,$size,$size,0)
  if($handle -eq [IntPtr]::Zero){throw "Cannot load embedded application icon at $size"}
  try{$icon=[Drawing.Icon]::FromHandle($handle);if($icon.Width -ne $size -or $icon.Height -ne $size){throw "Icon dimensions differ at $size"};Write-Output "PASS embedded icon ${size}x${size}"}finally{[void][PulseIconCheck]::DestroyIcon($handle)}
 }
}finally{[void][PulseIconCheck]::FreeLibrary($module)}
