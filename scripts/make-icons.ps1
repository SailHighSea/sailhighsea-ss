<#
  Draws the SimpleSync icon (blue ring gear with eight rounded teeth, dark centre, a sailboat with a
  white double-headed circular arrow underneath, the pair centred on the gear) and writes:
    assets\icon.svg         vector master
    assets\icon.png         1024 px
    assets\icon.ico         16, 24, 32, 48, 64, 128, 256 px (PNG-compressed entries)
    src\ui\icon_data.h      128 px PNG as a C array, used for the window icon and title bar logo

  Run after changing the artwork:  .\scripts\make-icons.ps1
  All geometry lives in this file, so the SVG and the bitmaps always match.
#>
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$inv = [Globalization.CultureInfo]::InvariantCulture
function F([double]$v) { [string]::Format($inv, '{0:0.##}', $v) }

# ---- geometry (128 x 128 canvas, centre 64,64) ---------------------------------------------
$C = 64.0
$ringOuter = 49.5; $ringInner = 33.0          # the gear ring
$teeth = 8; $toothW = 20.0; $toothR = 4.5     # eight rounded rectangular teeth
$toothTop = $C - 60.0; $toothBottom = $C - 44.0   # tooth spans radius 44..60 (overlaps the ring)

# Boat + double-headed arrow, in units relative to the gear centre. The stack spans y -22 .. +24, so it is
# shifted up by a little (stackShift) to put its middle exactly on the centre; it is symmetric left/right.
$stackShift = -2.95
# the boat is drawn smaller (scale 0.8) and moved up so the arrow heads clear the hull
$boatScale = 0.8; $boatDy = -1.0
function Boat($pts) { , @($pts | ForEach-Object { , @(($_[0] * $boatScale), (($_[1] * $boatScale) + $boatDy)) }) }
$sailL = Boat @(@(-2, -22), @(-2, 2), @(-15, 2))
$sailR = Boat @(@(2, -20), @(2, 2), @(15, 2))
$hull  = Boat @(@(-17, 5), @(17, 5), @(12.5, 11), @(-12.5, 11))
# Double-headed circular arrow: the lower part of a circle under the boat (a "smile"), with a head on each end.
$arrowR = 19.0; $arrowCy = 4.0; $arrowFrom = 40.0; $arrowTo = 140.0; $arrowStroke = 3.0
$headLen = 7.5; $headHalf = 4.2
function ArcPt([double]$deg) { $a = $deg * [Math]::PI / 180; @(($arrowR * [Math]::Cos($a)), ($arrowCy + $arrowR * [Math]::Sin($a))) }
$arrowLine = @(); for ($i = 0; $i -le 40; $i++) { $arrowLine += ,(ArcPt ($arrowFrom + ($arrowTo - $arrowFrom) * $i / 40)) }
function ArcHead([double]$deg, [double]$outward) {       # outward = -1 at the start of the arc (right end), +1 at the end (left end)
    $a = $deg * [Math]::PI / 180
    $p = ArcPt $deg; $rad = @([Math]::Cos($a), [Math]::Sin($a)); $dir = @((-$outward * [Math]::Sin($a)), ($outward * [Math]::Cos($a)))
    @(@(($p[0] + $headLen * $dir[0]), ($p[1] + $headLen * $dir[1])), @(($p[0] + $headHalf * $rad[0]), ($p[1] + $headHalf * $rad[1])), @(($p[0] - $headHalf * $rad[0]), ($p[1] - $headHalf * $rad[1])))
}
$arrowHeadR = ArcHead $arrowFrom -1                      # right end, pointing up and out
$arrowHeadL = ArcHead $arrowTo 1                         # left end, pointing up and out

$cGear1 = '#3B82F6'; $cGear2 = '#2563EB'     # gear gradient, top-left to bottom-right
$cDisc = '#0F172A'; $cWhite = '#FFFFFF'; $cSky = '#93C5FD'

# ---- SVG ---------------------------------------------------------------------------------------
function Pts($list) { ($list | ForEach-Object { (F $_[0]) + ',' + (F $_[1]) }) -join ' ' }
$rMid = ($ringOuter + $ringInner) / 2; $rW = $ringOuter - $ringInner
$toothSvg = (0..($teeth - 1) | ForEach-Object {
    "    <rect x=""$(F ($C - $toothW / 2))"" y=""$(F $toothTop)"" width=""$(F $toothW)"" height=""$(F ($toothBottom - $toothTop))"" rx=""$(F $toothR)"" transform=""rotate($(F ($_ * 360.0 / $teeth)) $(F $C) $(F $C))""/>"
}) -join "`n"
$svg = @"
<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128">
  <title>SailHighSea SimpleSync</title>
  <defs>
    <linearGradient id="gear" gradientUnits="userSpaceOnUse" x1="20" y1="4" x2="108" y2="124">
      <stop offset="0" stop-color="$cGear1"/>
      <stop offset="1" stop-color="$cGear2"/>
    </linearGradient>
  </defs>
  <g fill="url(#gear)">
$toothSvg
  </g>
  <circle cx="64" cy="64" r="$(F $rMid)" fill="none" stroke="url(#gear)" stroke-width="$(F $rW)"/>
  <circle cx="64" cy="64" r="$(F $ringInner)" fill="$cDisc"/>
  <g transform="translate(64 $(F ($C + $stackShift)))">
    <polygon points="$(Pts $sailL)" fill="$cWhite"/>
    <polygon points="$(Pts $sailR)" fill="$cSky"/>
    <polygon points="$(Pts $hull)" fill="$cWhite"/>
    <polyline points="$(Pts $arrowLine)" fill="none" stroke="$cWhite" stroke-width="$(F $arrowStroke)" stroke-linecap="round" stroke-linejoin="round"/>
    <polygon points="$(Pts $arrowHeadL)" fill="$cWhite" stroke="$cWhite" stroke-width="1.5" stroke-linejoin="round"/>
    <polygon points="$(Pts $arrowHeadR)" fill="$cWhite" stroke="$cWhite" stroke-width="1.5" stroke-linejoin="round"/>
  </g>
</svg>
"@
[IO.File]::WriteAllText((Join-Path $root 'assets\icon.svg'), $svg, (New-Object Text.UTF8Encoding $false))

# ---- bitmaps -------------------------------------------------------------------------------------
function Pf($list, [double]$k) { , @($list | ForEach-Object { New-Object Drawing.PointF ([single]($_[0] * $k)), ([single]($_[1] * $k)) }) }
function Col($hex) { [Drawing.ColorTranslator]::FromHtml($hex) }

function RoundRectPath([double]$x, [double]$y, [double]$w, [double]$h, [double]$r) {
    $p = New-Object Drawing.Drawing2D.GraphicsPath; $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90); $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90); $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90); $p.CloseFigure(); $p
}

function Render([int]$px) {
    $k = $px / 128.0
    $bmp = New-Object Drawing.Bitmap $px, $px, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'; $g.Clear([Drawing.Color]::Transparent)

    $gearBrush = New-Object Drawing.Drawing2D.LinearGradientBrush (New-Object Drawing.PointF ([single](20 * $k)), ([single](4 * $k))), (New-Object Drawing.PointF ([single](108 * $k)), ([single](124 * $k))), (Col $cGear1), (Col $cGear2)

    # ring (annulus)
    $ring = New-Object Drawing.Drawing2D.GraphicsPath ([Drawing.Drawing2D.FillMode]::Alternate)
    $ring.AddEllipse([single](($C - $ringOuter) * $k), [single](($C - $ringOuter) * $k), [single]($ringOuter * 2 * $k), [single]($ringOuter * 2 * $k))
    $ring.AddEllipse([single](($C - $ringInner) * $k), [single](($C - $ringInner) * $k), [single]($ringInner * 2 * $k), [single]($ringInner * 2 * $k))
    $g.FillPath($gearBrush, $ring)

    # teeth
    for ($i = 0; $i -lt $teeth; $i++) {
        $t = RoundRectPath (($C - $toothW / 2) * $k) ($toothTop * $k) ($toothW * $k) (($toothBottom - $toothTop) * $k) ($toothR * $k)
        $m = New-Object Drawing.Drawing2D.Matrix
        $m.RotateAt([single]($i * 360.0 / $teeth), (New-Object Drawing.PointF ([single]($C * $k)), ([single]($C * $k))))
        $t.Transform($m)
        $g.FillPath($gearBrush, $t)
    }

    # dark centre
    $d = $ringInner * 2 * $k; $o = ($C - $ringInner) * $k
    $g.FillEllipse((New-Object Drawing.SolidBrush (Col $cDisc)), [single]$o, [single]$o, [single]$d, [single]$d)

    # boat + double-headed arrow, origin on the gear centre
    $g.TranslateTransform([single]($C * $k), [single](($C + $stackShift) * $k)); $g.ScaleTransform([single]$k, [single]$k)
    $g.FillPolygon((New-Object Drawing.SolidBrush (Col $cWhite)), (Pf $sailL 1))
    $g.FillPolygon((New-Object Drawing.SolidBrush (Col $cSky)),   (Pf $sailR 1))
    $g.FillPolygon((New-Object Drawing.SolidBrush (Col $cWhite)), (Pf $hull 1))
    $w = if ($px -le 32) { 5.5 } else { $arrowStroke }
    $shaft = New-Object Drawing.Pen (Col $cWhite), ([single]$w); $shaft.StartCap = 'Round'; $shaft.EndCap = 'Round'; $shaft.LineJoin = 'Round'
    $g.DrawLines($shaft, (Pf $arrowLine 1))
    $edge = New-Object Drawing.Pen (Col $cWhite), ([single]1.5); $edge.LineJoin = 'Round'
    foreach ($h in @($arrowHeadL, $arrowHeadR)) {
        $g.FillPolygon((New-Object Drawing.SolidBrush (Col $cWhite)), (Pf $h 1)); $g.DrawPolygon($edge, (Pf $h 1))
    }
    $g.Dispose()
    $bmp
}

function PngBytes($bmp) { $ms = New-Object IO.MemoryStream; $bmp.Save($ms, [Drawing.Imaging.ImageFormat]::Png); ,$ms.ToArray() }

$big = Render 1024; $big.Save((Join-Path $root 'assets\icon.png'), [Drawing.Imaging.ImageFormat]::Png); $big.Dispose()

$sizes = 16, 24, 32, 48, 64, 128, 256
$images = foreach ($sz in $sizes) { $b = Render $sz; $bytes = PngBytes $b; $b.Dispose(); ,$bytes }
$ms = New-Object IO.MemoryStream; $bw = New-Object IO.BinaryWriter $ms
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $sz = $sizes[$i]; $dim = if ($sz -ge 256) { 0 } else { $sz }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32); $bw.Write([uint32]$images[$i].Length); $bw.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($img in $images) { $bw.Write($img) }
[IO.File]::WriteAllBytes((Join-Path $root 'assets\icon.ico'), $ms.ToArray())

$b = Render 128; $png = PngBytes $b; $b.Dispose()
$sb = New-Object Text.StringBuilder
[void]$sb.AppendLine('/* Generated by scripts/make-icons.ps1 - do not edit. 128 px PNG used for the window icon and title bar logo. */')
[void]$sb.AppendLine('#ifndef SHS_ICON_DATA_H')
[void]$sb.AppendLine('#define SHS_ICON_DATA_H')
[void]$sb.AppendLine("static const unsigned char SHS_ICON_PNG[$($png.Length)] = {")
for ($i = 0; $i -lt $png.Length; $i += 16) {
    $n = [Math]::Min(16, $png.Length - $i)
    [void]$sb.AppendLine('    ' + (($png[$i..($i + $n - 1)] | ForEach-Object { '0x{0:X2}' -f $_ }) -join ', ') + ',')
}
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('#endif')
[IO.File]::WriteAllText((Join-Path $root 'src\ui\icon_data.h'), $sb.ToString(), (New-Object Text.UTF8Encoding $false))

Write-Host "Wrote assets\icon.svg, icon.png, icon.ico and src\ui\icon_data.h ($($png.Length) byte window icon)" -ForegroundColor Green
