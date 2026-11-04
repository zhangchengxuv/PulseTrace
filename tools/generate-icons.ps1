# Reproducible vector drawing, rendered at 4x and reduced for each Windows icon size.
# No external packages, fonts, network, or generated third-party artwork.
param([switch]$WritePreviews)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$root=Split-Path -Parent $PSScriptRoot
$folder=Join-Path $root 'assets'
New-Item -ItemType Directory -Force -Path $folder | Out-Null
$frames=@()
foreach($size in @(16,24,32,48,64,128,256)){
    $bitmap=New-Object Drawing.Bitmap ($size*4),($size*4)
    $g=[Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode='AntiAlias';$g.ScaleTransform(($size*4/256),($size*4/256))
    $path=New-Object Drawing.Drawing2D.GraphicsPath
    $path.AddArc(8,8,104,104,180,90);$path.AddArc(144,8,104,104,270,90)
    $path.AddArc(144,144,104,104,0,90);$path.AddArc(8,144,104,104,90,90);$path.CloseFigure()
    $brush=New-Object Drawing.SolidBrush ([Drawing.ColorTranslator]::FromHtml('#0a2639'))
    $g.FillPath($brush,$path)
    $pen=New-Object Drawing.Pen ([Drawing.ColorTranslator]::FromHtml('#3498db')),17
    $pen.StartCap='Round';$pen.EndCap='Round';$pen.LineJoin='Round'
    $points=[Drawing.PointF[]]@((New-Object Drawing.PointF 46,139),(New-Object Drawing.PointF 82,139),(New-Object Drawing.PointF 104,79),(New-Object Drawing.PointF 137,181),(New-Object Drawing.PointF 162,116),(New-Object Drawing.PointF 210,116))
    $g.DrawLines($pen,$points)
    $dot=New-Object Drawing.SolidBrush ([Drawing.ColorTranslator]::FromHtml('#8bc4ea'))
    $g.FillEllipse($dot,198,104,24,24)
    $small=New-Object Drawing.Bitmap $size,$size
    $down=[Drawing.Graphics]::FromImage($small);$down.InterpolationMode='HighQualityBicubic'
    $down.DrawImage($bitmap,0,0,$size,$size)
    $stream=New-Object IO.MemoryStream
    $small.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
    $frames+=,[pscustomobject]@{Size=$size;Bytes=$stream.ToArray()}
    if($WritePreviews){
        $previewFolder=Join-Path $root 'build\icon-previews'
        New-Item -ItemType Directory -Force -Path $previewFolder | Out-Null
        $small.Save((Join-Path $previewFolder "PulseTrace-$size.png"),[Drawing.Imaging.ImageFormat]::Png)
    }
    $stream.Dispose();$down.Dispose();$small.Dispose();$dot.Dispose();$pen.Dispose();$brush.Dispose();$path.Dispose();$g.Dispose();$bitmap.Dispose()
}
$file=[IO.File]::Create((Join-Path $folder 'PulseTrace.ico'));$writer=New-Object IO.BinaryWriter $file
$writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$frames.Count)
$offset=6+16*$frames.Count
foreach($frame in $frames){$s=if($frame.Size -eq 256){0}else{$frame.Size};$writer.Write([byte]$s);$writer.Write([byte]$s);$writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]$frame.Bytes.Length);$writer.Write([uint32]$offset);$offset+=$frame.Bytes.Length}
foreach($frame in $frames){$writer.Write([byte[]]$frame.Bytes)}
$writer.Dispose();$file.Dispose()
Write-Output 'Generated assets/PulseTrace.ico: 16,24,32,48,64,128,256. Optional previews go to build/icon-previews.'
