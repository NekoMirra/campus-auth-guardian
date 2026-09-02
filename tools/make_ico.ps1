Add-Type -AssemblyName System.Drawing
$sizes = @(16, 32, 48, 256)
$pngs = @()
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.Rectangle(0,0,$s,$s)),
        [System.Drawing.Color]::FromArgb(255,0,120,212),
        [System.Drawing.Color]::FromArgb(255,0,70,150), 45)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = [Math]::Max(2, [int]($s * 0.22))
    $path.AddArc(0, 0, $r*2, $r*2, 180, 90)
    $path.AddArc($s-$r*2, 0, $r*2, $r*2, 270, 90)
    $path.AddArc($s-$r*2, $s-$r*2, $r*2, $r*2, 0, 90)
    $path.AddArc(0, $s-$r*2, $r*2, $r*2, 90, 90)
    $path.CloseFigure()
    $g.FillPath($brush, $path)
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::White, [Math]::Max(1.0, $s * 0.08))
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    $cx = $s / 2; $cy = $s * 0.72
    foreach ($k in @(0.14, 0.28, 0.42)) {
        $g.DrawArc($pen, $cx - $s*$k, $cy - $s*$k, $s*$k*2, $s*$k*2, 205, 130)
    }
    $dot = [Math]::Max(1.5, $s * 0.06)
    $g.FillEllipse([System.Drawing.Brushes]::White, $cx - $dot, $cy - $dot, $dot*2, $dot*2)
    $pngMs = New-Object System.IO.MemoryStream
    $bmp.Save($pngMs, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs += ,($pngMs.ToArray())
    $g.Dispose(); $brush.Dispose(); $pen.Dispose(); $bmp.Dispose()
}
$icoPath = 'C:\Users\NekoMirra\campus-auth-guardian\csharp\app.ico'
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$pngs.Count)
$offset = 6 + 16 * $pngs.Count
for ($i = 0; $i -lt $pngs.Count; $i++) {
    $size = $sizes[$i]
    $bw.Write([byte]($size -band 0xFF)); $bw.Write([byte]0)
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$pngs[$i].Length); $bw.Write([uint32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($png in $pngs) { $bw.Write($png) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($icoPath, $ms.ToArray())
Write-Host "ICO-OK $((Get-Item $icoPath).Length) bytes"
