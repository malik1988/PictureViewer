param(
    [string]$Output = (Join-Path $PSScriptRoot '..\resources\picture_viewer.ico')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Scale([double]$value, [double]$factor) {
    return [float]($value * $factor)
}

function Add-RoundedRect([System.Drawing.Drawing2D.GraphicsPath]$path,
                         [float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $path.AddArc($x, $y, $r, $r, 180, 90)
    $path.AddArc($x + $w - $r, $y, $r, $r, 270, 90)
    $path.AddArc($x + $w - $r, $y + $h - $r, $r, $r, 0, 90)
    $path.AddArc($x, $y + $h - $r, $r, $r, 90, 90)
    $path.CloseFigure()
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$images = [System.Collections.Generic.List[byte[]]]::new()

foreach ($size in $sizes) {
    $k = $size / 256.0
    $bitmap = [System.Drawing.Bitmap]::new($size, $size)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    # 深色圆角底板
    $backRect = [System.Drawing.RectangleF]::new(0, 0, $size, $size)
    $backBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        $backRect,
        [System.Drawing.Color]::FromArgb(255, 46, 64, 92),
        [System.Drawing.Color]::FromArgb(255, 16, 24, 40),
        90.0)
    $backPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
    Add-RoundedRect $backPath (Scale 2 $k) (Scale 2 $k) (Scale 252 $k) (Scale 252 $k) (Scale 44 $k)
    $g.FillPath($backBrush, $backPath)

    $edgePen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(110, 150, 185, 220), (Scale 2 $k))
    $g.DrawPath($edgePen, $backPath)

    # 白色相框
    $framePen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 242, 246, 252), (Scale 11 $k))
    $framePath = [System.Drawing.Drawing2D.GraphicsPath]::new()
    Add-RoundedRect $framePath (Scale 48 $k) (Scale 54 $k) (Scale 160 $k) (Scale 150 $k) (Scale 9 $k)
    $g.DrawPath($framePen, $framePath)

    $skyBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        [System.Drawing.RectangleF]::new((Scale 54 $k), (Scale 60 $k), (Scale 148 $k), (Scale 138 $k)),
        [System.Drawing.Color]::FromArgb(255, 86, 152, 196),
        [System.Drawing.Color]::FromArgb(255, 34, 66, 108),
        90.0)
    $skyPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
    Add-RoundedRect $skyPath (Scale 54 $k) (Scale 60 $k) (Scale 148 $k) (Scale 138 $k) (Scale 5 $k)
    $g.FillPath($skyBrush, $skyPath)

    # 太阳
    $sunBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 255, 214, 112))
    $g.FillEllipse($sunBrush, (Scale 152 $k), (Scale 80 $k), (Scale 34 $k), (Scale 34 $k))

    # 远山与近山
    $farBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 72, 156, 164))
    $farPoints = [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new((Scale 58 $k), (Scale 190 $k)),
        [System.Drawing.PointF]::new((Scale 112 $k), (Scale 124 $k)),
        [System.Drawing.PointF]::new((Scale 166 $k), (Scale 190 $k))
    )
    $g.FillPolygon($farBrush, $farPoints)

    $nearBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 36, 96, 112))
    $nearPoints = [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new((Scale 122 $k), (Scale 192 $k)),
        [System.Drawing.PointF]::new((Scale 174 $k), (Scale 138 $k)),
        [System.Drawing.PointF]::new((Scale 202 $k), (Scale 192 $k))
    )
    $g.FillPolygon($nearBrush, $nearPoints)

    $stream = [System.IO.MemoryStream]::new()
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $images.Add($stream.ToArray())

    $g.Dispose()
    $bitmap.Dispose()
    $stream.Dispose()
}

# ICO 容器：PNG 压缩的图像条目，Vista 及以上原生支持。
$outputStream = [System.IO.MemoryStream]::new()
$writer = [System.IO.BinaryWriter]::new($outputStream)
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$sizes.Count)

$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $bytes = $images[$i]
    $dim = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
    $writer.Write([byte]$dim)
    $writer.Write([byte]$dim)
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$bytes.Length)
    $writer.Write([uint32]$offset)
    $offset += $bytes.Length
}
foreach ($bytes in $images) {
    $writer.Write($bytes)
}
$writer.Flush()

$directory = Split-Path -Parent $Output
if (-not (Test-Path $directory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
[System.IO.File]::WriteAllBytes($Output, $outputStream.ToArray())
$writer.Dispose()
$outputStream.Dispose()

$icon = [System.Drawing.Icon]::new($Output)
"Generated $Output ($($icon.Width)x$($icon.Height), $((Get-Item $Output).Length) bytes)"
$icon.Dispose()
