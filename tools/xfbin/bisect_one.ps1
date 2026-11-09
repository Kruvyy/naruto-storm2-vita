# Bisection helper: rewrites ONE stage file in storm2\game_half (an uncompressed
# copy made with halve_textures.py --unpack-only) with textures a:b halved,
# then runs the PC battle replay and reports the last frame and any crash.
#   .\tools\xfbin\bisect_one.ps1 -File sd00b02.xfbin -Range 0:14 -Name B1
param([Parameter(Mandatory)] [string] $File, [string] $Range = '0:100000',
      [Parameter(Mandatory)] [string] $Name, [int] $Seconds = 240)
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
$tmp = Join-Path $env:TEMP "xfbin_bisect"
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$tmp\in\data\stage" | Out-Null
Copy-Item "storm2\game\data\stage\$File" "$tmp\in\data\stage\$File"
py -3 -I tools\xfbin\halve_textures.py "$tmp\in" "$tmp\out" tools\xfbin\build\xctd_unpack.exe "$tmp\scratch.bin" --bisect-range $Range
Copy-Item "$tmp\out\data\stage\$File" "storm2\game_half\data\stage\$File" -Force
$r = $root
.\scripts\run_storm2_stats.ps1 -Nome "bisect_$Name" -Segundos $Seconds -Jogo 'storm2\game_half' -Extra @('--gpu_backend=gles', "--input_replay=`"$r\docs\logs\I1_inputs\free_battle_slow.txt`"") | Select-Object -First 1
"last frame: " + (Import-Csv "docs\logs\bisect_$Name\frame_stats.csv" | Select-Object -Last 1).frame
Select-String -Path "docs\logs\bisect_$Name\storm2.log" -Pattern "Unhandled guest" | ForEach-Object { $_.Line.Substring(0, 110) }
# Put the plain uncompressed file back for the next test.
py -3 -I tools\xfbin\halve_textures.py "$tmp\in" "$tmp\plain" tools\xfbin\build\xctd_unpack.exe "$tmp\scratch.bin" --unpack-only | Out-Null
Copy-Item "$tmp\plain\data\stage\$File" "storm2\game_half\data\stage\$File" -Force
