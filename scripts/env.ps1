# Ambiente de build do port Storm 2 -> PS Vita.
# Uso: . .\scripts\env.ps1   (dot-source, para manter as variaveis na sessao)
$env:PORT_ROOT = Split-Path -Parent $PSScriptRoot
$env:VITASDK   = 'C:\vitasdk'

$paths = @(
  'C:\LLVM\bin',
  'C:\Users\minoo\p3tools\cmake\bin',
  'C:\Users\minoo\p3tools',
  'C:\Users\minoo\p3tools\cmd',
  "$env:VITASDK\bin"
)
foreach ($p in $paths) {
  if (-not (Test-Path $p)) { throw "Ferramenta ausente: $p" }
  if (-not ($env:PATH.Split(';') -contains $p)) { $env:PATH = "$p;$env:PATH" }
}

# Entrada original do jogo (somente leitura).
$env:STORM2_GAME = Join-Path $env:USERPROFILE 'Downloads\Naruto Storm 2 PT-BR - TokyoGames\Naruto Storm 2 PT-BR - TokyoGames\Naruto Shippuden Storm 2 PTBR'
