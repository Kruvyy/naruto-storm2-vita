# Grava uma sessao jogada (D3D12, com imagem) para reproduzir depois sem
# graficos: o runtime escreve docs\logs\<Nome>\input.txt (--input_record) e a
# janela e capturada a cada -Intervalo s (storm2\captures\<Nome>, fora do git)
# para conferir depois em que tela cada trecho estava. Termina quando a janela
# e fechada.
#
# Uso: .\scripts\record_session.ps1 -Nome sessao1 [-Intervalo 10] [-Extra '--mnk_mode=true']
#
# Para a reproducao funcionar tambem em maquinas mais lentas (Vita), espere
# ~2 s depois que cada tela aparecer antes de apertar o proximo botao.
param([Parameter(Mandatory)] [string] $Nome, [int] $Intervalo = 10, [string[]] $Extra = @())
$root = Split-Path -Parent $PSScriptRoot
$run = (New-Item -ItemType Directory -Force (Join-Path $root "docs\logs\$Nome")).FullName
$rec = Join-Path $run 'input.txt'
& (Join-Path $PSScriptRoot 'capture_storm2.ps1') -Nome $Nome -Segundos 0 -Intervalo $Intervalo `
  -Extra (@("--input_record=`"$rec`"") + $Extra)
if (Test-Path $rec) {
  $s = @(Select-String -Path $rec -Pattern '^S ').Count
  $k = @(Select-String -Path $rec -Pattern '^K ').Count
  "gravacao: $rec ($s mudancas de estado, $k keystrokes)"
} else { 'nenhuma gravacao foi escrita (controle conectado?)' }
