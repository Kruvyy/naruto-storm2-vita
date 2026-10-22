# Substitui links simbolicos do git (modo 120000) que o Windows sem Developer Mode
# grava como arquivos de texto contendo o caminho do destino. Copia o conteudo real.
# Uso: . .\scripts\env.ps1; .\scripts\fix_symlinks.ps1 -Repo .\rexglue
param([Parameter(Mandatory)] [string] $Repo)

$repoAbs = (Resolve-Path $Repo).Path
$roots = @($repoAbs)
$subs = git -C $repoAbs submodule foreach --quiet --recursive 'echo $displaypath'
foreach ($s in $subs) { $roots += (Join-Path $repoAbs $s) }

$fixed = 0
foreach ($root in $roots) {
  $links = git -C $root ls-files -s | Where-Object { $_ -match '^120000 ' }
  foreach ($l in $links) {
    $rel = ($l -split "`t", 2)[1]
    $file = Join-Path $root $rel
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { continue }
    $item = Get-Item -LiteralPath $file
    if ($item.LinkType) { continue }               # ja e um link real
    $target = (Get-Content -LiteralPath $file -Raw).Trim()
    $src = Join-Path (Split-Path -Parent $file) $target
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) {
      Write-Warning "Destino ausente: $rel -> $target"; continue
    }
    Copy-Item -LiteralPath $src -Destination $file -Force
    git -C $root update-index --assume-unchanged -- $rel
    "corrigido: $root :: $rel"
    $fixed++
  }
}
"total corrigido: $fixed"
