# Testes A/B de opcoes de GPU para a tela preta: roda o Storm 2 com cada
# conjunto de opcoes e resume as linhas "swap-diag ... texture content" do log
# (conteudo do frontbuffer lido da GPU, antes do gama) e a captura da janela.
#
# Uso: .\scripts\gpu_ab_storm2.ps1 -Prefixo D25 -Segundos 45
param([string] $Prefixo = 'AB', [int] $Segundos = 45, [string[]] $So = @())
$variantes = [ordered]@{
  'base'        = @()
  'rov'         = @('--render_target_path_d3d12=rov')
  'no_direct'   = @('--direct_host_resolve=false')
  'readback'    = @('--d3d12_readback_resolve=true')
  'gamma8'      = @('--gamma_render_target_as_unorm16=false')
  'no_mrtclamp' = @('--mrt_edram_used_range_clamp_to_min=false')
  'vs_cpu'      = @('--execute_unclipped_draw_vs_on_cpu=true')
}
foreach ($k in $variantes.Keys) {
  if ($So.Count -and $So -notcontains $k) { continue }
  $nome = "${Prefixo}_$k"
  $extra = @('--gpu_swap_diag_interval=300') + $variantes[$k]
  $cap = & (Join-Path $PSScriptRoot 'capture_storm2.ps1') -Nome $nome -Segundos $Segundos -Intervalo $Segundos -Extra $extra
  $log = Join-Path (Split-Path -Parent $PSScriptRoot) "docs\logs\$nome\storm2.log"
  $lit = Select-String -Path $log -Pattern 'texture content.*lit ([0-9.]+)%' | ForEach-Object { $_.Matches[0].Groups[1].Value }
  "{0,-10} lit% por relatorio: {1} | janela: {2}" -f $k, ($lit -join ' '), (($cap | Select-String 'brilho') -join ' ')
}
