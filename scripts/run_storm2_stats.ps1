# Roda o Storm 2 recompilado (x64) com as medicoes por quadro ligadas.
# Saida em docs\logs\<Nome>\: frame_stats.csv, import_stats.csv,
# thread_insns.csv e storm2.log.
#
# Uso:
#   .\scripts\run_storm2_stats.ps1 -Nome D17_run3 -Segundos 240   (encerra sozinho)
#   .\scripts\run_storm2_stats.ps1 -Nome batalha1 -Segundos 0     (espera voce fechar a janela)
#   .\scripts\run_storm2_stats.ps1 -Nome batalha1 -Segundos 0 -Extra '--mnk_mode=true'  (teclado)
#   .\scripts\run_storm2_stats.ps1 -Nome E12 -Build softmmu -Extra '--gpu_backend=null'
#     (memoria por software, como no Vita; so funciona com a GPU nula)
param(
  [Parameter(Mandatory)] [string] $Nome,
  [int] $Segundos = 240,
  [string[]] $Extra = @(),
  [ValidateSet('release', 'softmmu')] [string] $Build = 'release'
)
$root = Split-Path -Parent $PSScriptRoot
$bd = Join-Path $root "storm2\out\build\win-amd64-$Build"
$sdk = 'win-amd64'
if ($Build -eq 'softmmu') { $sdk = 'win-amd64-softmmu' }
$sdkbin = Join-Path $root "rexglue\out\install\$sdk\bin"
Copy-Item (Join-Path $sdkbin 'rexruntime.dll'), (Join-Path $sdkbin 'rexgpu-xenos.dll') $bd -Force
$run = (New-Item -ItemType Directory -Force (Join-Path $root "docs\logs\$Nome")).FullName
$exe = Join-Path $bd 'storm2.exe'
$game = Join-Path $root 'storm2\game'   # copia dos dados; o original nunca e usado
$a = @("--game_data_root=`"$game`"", '--gpu_plugin=xenos', '--perf_frame_stats=true',
       "--perf_frame_stats_file=`"$run\frame_stats.csv`"",
       "--perf_import_stats_file=`"$run\import_stats.csv`"",
       "--log_file=`"$run\storm2.log`"", '--fullscreen=false',
       '--window_width=1280', '--window_height=720') + $Extra
$p = Start-Process -FilePath $exe -ArgumentList $a -WorkingDirectory $run -PassThru
$t0 = Get-Date
while (-not $p.HasExited -and ($Segundos -le 0 -or ((Get-Date) - $t0).TotalSeconds -lt $Segundos)) {
  Start-Sleep -Seconds 3
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Start-Sleep 2; "encerrado apos $Segundos s" }
else { "saiu com codigo 0x{0:X8}" -f $p.ExitCode }
"resultados em $run"
