# Reads the 8x4 cells of a gles_debug_resolve_meter image (PC dump or Vita3K
# capture): red = sharpness x4, green = mean brightness, both 0..255. Each
# cell is sampled near its bottom (the Vita3K FPS overlay covers the top left).
#   .\tools\meter_read.ps1 storm2\captures\vita3k\x_0.png [-Compare other.png]
param([Parameter(Mandatory)] [string] $Image, [string] $Compare = '')
Add-Type -AssemblyName System.Drawing
function Read-Cells([string] $path) {
  $bmp = [System.Drawing.Bitmap]::FromFile((Resolve-Path $path).Path)
  $cells = @()
  for ($y = 0; $y -lt 4; ++$y) {
    for ($x = 0; $x -lt 8; ++$x) {
      $px = [int](($x + 0.5) * $bmp.Width / 8)
      $py = [int](($y + 0.93) * $bmp.Height / 4)
      $c = $bmp.GetPixel($px, $py)
      $cells += , @($c.R, $c.G)
    }
  }
  $bmp.Dispose()
  return , $cells
}
$a = Read-Cells $Image
$b = if ($Compare) { Read-Cells $Compare } else { $null }
"sharpness (red):"
for ($y = 0; $y -lt 4; ++$y) {
  $row = for ($x = 0; $x -lt 8; ++$x) {
    $i = $y * 8 + $x
    if ($b) { "{0,3}/{1,-3}" -f $a[$i][0], $b[$i][0] } else { "{0,3}" -f $a[$i][0] }
  }
  "  " + ($row -join " ")
}
"brightness (green):"
for ($y = 0; $y -lt 4; ++$y) {
  $row = for ($x = 0; $x -lt 8; ++$x) {
    $i = $y * 8 + $x
    if ($b) { "{0,3}/{1,-3}" -f $a[$i][1], $b[$i][1] } else { "{0,3}" -f $a[$i][1] }
  }
  "  " + ($row -join " ")
}
if ($b) {
  $sa = ($a | ForEach-Object { $_[0] } | Measure-Object -Sum).Sum
  $sb = ($b | ForEach-Object { $_[0] } | Measure-Object -Sum).Sum
  "total sharpness: $sa / $sb (first / second)"
}
