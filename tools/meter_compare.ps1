# Runs the resolve meter (gles_debug_resolve_meter) for each target on the PC
# (GLES, menu replay, frame dump) and in the test Vita3K copy, then prints the
# cell values side by side (PC / Vita3K).
#   .\tools\meter_compare.ps1 -Targets 0DF22000:0,0E242000:0
param([Parameter(Mandatory)] [string[]] $Targets, [int] $Frame = 2580)
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
foreach ($t in $Targets) {
  $n = $t.Replace(':', '_')
  $d = "$root\storm2\gles_frames\meter_pc_$n"
  New-Item -ItemType Directory -Force $d | Out-Null
  .\scripts\run_storm2_stats.ps1 -Nome "meter_pc_$n" -Segundos 95 -Extra @('--gpu_backend=gles',
    "--input_replay=`"$root\docs\logs\I1_inputs\menu_start_repeat.txt`"", "--gles_dump_dir=`"$d`"",
    "--gles_debug_show_resolve=$t", '--gles_debug_resolve_meter=true') | Out-Null
  .\scripts\v3k_test.ps1 -Name "meter_v3k_$n" -Frames 2600 -Config config_res1.yml -Toml @(
    "gles_debug_show_resolve = `"$t`"", 'gles_debug_resolve_meter = true') | Out-Null
  $pc = Get-ChildItem $d -Filter "frame_*.png" | Sort-Object Name | Where-Object { [int]($_.BaseName -replace '\D', '') -ge $Frame } | Select-Object -First 1
  "===== $t  (PC $($pc.Name) / Vita3K)"
  & "$root\tools\meter_read.ps1" -Image $pc.FullName -Compare "$root\storm2\captures\vita3k\meter_v3k_${n}_0.png"
}
