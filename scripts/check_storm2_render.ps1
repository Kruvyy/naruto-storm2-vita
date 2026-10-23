# Regressao da tela preta (bug 19 em docs/bugs.md): roda o Storm 2 x64 com as
# opcoes padrao do runtime (so o diagnostico ligado) e exige conteudo no
# frontbuffer lido da GPU e na janela. Precisa dos dados do jogo (local).
#
# Uso: .\scripts\check_storm2_render.ps1 [-Nome R1_render] [-Segundos 60]
# Saida: codigo 0 = passou, 1 = falhou.
param([string] $Nome = 'R_render', [int] $Segundos = 60)
$cap = & (Join-Path $PSScriptRoot 'capture_storm2.ps1') -Nome $Nome -Segundos $Segundos `
  -Intervalo 15 -Extra @('--gpu_swap_diag_interval=300')
$cap
$log = Join-Path (Split-Path -Parent $PSScriptRoot) "docs\logs\$Nome\storm2.log"
$lit = @(Select-String -Path $log -Pattern 'texture content.*lit ([0-9.]+)%' |
  ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
$win = @($cap | Select-String 'nao_preto=([0-9.]+) %' |
  ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
$litMax = ($lit | Measure-Object -Maximum).Maximum
$winMax = ($win | Measure-Object -Maximum).Maximum
"frontbuffer lit% max: $litMax (relatorios: $($lit.Count)); janela nao_preto% max: $winMax"
if ($lit.Count -gt 0 -and $litMax -gt 1 -and $winMax -gt 1) { 'PASSOU'; exit 0 }
'FALHOU: frontbuffer ou janela sem conteudo'; exit 1
