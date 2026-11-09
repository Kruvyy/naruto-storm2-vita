# Runs storm2_vita in the test Vita3K copy (vita3k_test\, outside %TEMP%; the
# user's own Vita3K and its VitaFS are not touched). Copies the current eboot,
# writes the given storm2.toml lines, waits for frame $Frames (storm2_stats.log),
# takes one capture and closes. Gives up after $Timeout s or $Stall s without
# progress.
#   .\scripts\v3k_test.ps1 -Name t1 -Toml @('soft_mmu_caps = "0xAB000000=120"')
param([Parameter(Mandatory)] [string] $Name, [string[]] $Toml = @(), [int] $Frames = 8200,
      [int] $Timeout = 1100, [int] $Stall = 150, [switch] $NoReplay)
$root = Split-Path -Parent $PSScriptRoot
$t = Join-Path $root 'vita3k_test'
$exe = Join-Path $t 'bin\Vita3K.exe'
$data = Join-Path $t 'fs\ux0\data\storm2port'
Get-Process Vita3K -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $exe } | Stop-Process -Force
Start-Sleep 2
Copy-Item (Join-Path $root 'vita\storm2\build-vita\eboot.bin') (Join-Path $t 'fs\ux0\app\RXGT00006\eboot.bin') -Force
Remove-Item "$data\storm2_stats.log" -ErrorAction SilentlyContinue
$lines = @('# Vita3K test run (emulator only)', 'log_flush_interval = 1', 'gpu_thread_priority_boost = 1',
           'gles_surface_texture_min_kib = 3000', 'gles_surface_texture_refresh_swaps = 1')
if (-not $NoReplay) { $lines += 'input_replay = "ux0:data/storm2port/free_battle_slow.txt"' }
Set-Content -Encoding ascii "$data\storm2.toml" ($lines + $Toml)
Start-Process $exe -ArgumentList '-c', "`"$t\config.yml`"", '-w', '-r', 'RXGT00006' -WorkingDirectory (Split-Path $exe)
$start = Get-Date; $last = -1; $lastChange = Get-Date; $cur = 0
while ($true) {
  Start-Sleep 5
  $line = Get-Content "$data\storm2_stats.log" -Tail 6 -ErrorAction SilentlyContinue | Where-Object { $_ -match 'frames=' } | Select-Object -Last 1
  if ($line -match 'frames=(\d+)') { $cur = [int]$Matches[1] }
  if ($cur -ne $last) { $last = $cur; $lastChange = Get-Date }
  if ($cur -ge $Frames) { break }
  if (((Get-Date) - $start).TotalSeconds -gt $Timeout) { "timeout at frame $cur"; break }
  if ($cur -gt 0 -and ((Get-Date) - $lastChange).TotalSeconds -gt $Stall) { "stalled at frame $cur"; break }
  if (-not (Get-Process Vita3K -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $exe })) { "Vita3K exited at frame $cur"; break }
}
"reached frame $cur after $([int]((Get-Date) - $start).TotalSeconds) s"
if (Get-Process Vita3K -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $exe }) {
  & "$root\scripts\capture_vita3k.ps1" -Saida "$root\storm2\captures\vita3k\${Name}_0.png" -Exe $exe | Out-Null
}
Get-Process Vita3K -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $exe } | Stop-Process -Force
Select-String -Path "$data\storm2_stats.log" -Pattern 'ERROR' | Select-Object -Last 2 | ForEach-Object { $_.Line }
Select-String -Path "$data\storm2.log" -Pattern 'Software MMU: the guest wrote past|FATAL|out of host memory' | Select-Object -Last 3 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(250, $_.Line.Length)) }
