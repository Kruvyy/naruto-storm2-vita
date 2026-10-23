# Reproduz uma gravacao de record_session.ps1 e captura a janela, para
# conferir se a reproducao chega ao mesmo ponto (por exemplo, a batalha).
#   -Backend d3d12 : com imagem (as capturas mostram as telas)
#   -Backend null  : sem renderizar (capturas pretas; conferir pelo log e
#                    pelas estatisticas por quadro)
#
# Uso: .\scripts\replay_session.ps1 -Gravacao docs\logs\sessao1\input.txt -Nome replay1 `
#        [-Backend d3d12|null] [-Segundos 300] [-Stretch 1.0] [-Build release|softmmu]
#   -Build softmmu: memoria por software, como no Vita (exige -Backend null)
param(
  [Parameter(Mandatory)] [string] $Gravacao,
  [Parameter(Mandatory)] [string] $Nome,
  [ValidateSet('d3d12', 'null')] [string] $Backend = 'd3d12',
  [int] $Segundos = 300,
  [double] $Stretch = 1.0,
  [int] $Intervalo = 15,
  [ValidateSet('release', 'softmmu')] [string] $Build = 'release'
)
if ($Build -eq 'softmmu' -and $Backend -ne 'null') { throw '-Build softmmu so funciona com -Backend null' }
$rec = (Resolve-Path $Gravacao).Path
$inv = [Globalization.CultureInfo]::InvariantCulture
$extra = @("--input_replay=`"$rec`"", ('--input_replay_stretch=' + $Stretch.ToString($inv)),
           "--gpu_backend=$Backend", '--perf_frame_stats=true')
& (Join-Path $PSScriptRoot 'capture_storm2.ps1') -Nome $Nome -Segundos $Segundos -Intervalo $Intervalo -Extra $extra -Build $Build
