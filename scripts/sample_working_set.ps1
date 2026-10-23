# Amostra a memoria residente (QueryWorkingSetEx) do storm2.exe em execucao a
# cada -Intervalo segundos, ate o processo terminar. Grava CSV com numeros em
# formato invariante (ponto decimal):
#   segundos, residente por bloco de 256 MiB do espaco do convidado
#   (visao virtual 0x1_0000_0000 + endereco), residente da base fisica
#   (0x2_0000_0000, 512 MiB), working set e memoria privada do processo.
# As visoes 0xA0000000-0xFFFFFFFF do convidado aliam a memoria fisica; cada
# visao tem seus proprios PTEs, entao paginas tocadas por duas visoes aparecem
# nas duas. Por isso os blocos sao gravados separados.
#
# Uso: .\scripts\sample_working_set.ps1 -Saida docs\logs\X\working_set.csv [-Intervalo 20]
param([Parameter(Mandatory)] [string] $Saida, [int] $Intervalo = 20, [int] $EsperaInicio = 90)

$src = Get-Content (Join-Path $PSScriptRoot 'guest_working_set.ps1') -Raw
$a = $src.IndexOf('Add-Type'); $b = $src.IndexOf("'@", $a) + 2
Invoke-Expression $src.Substring($a, $b - $a)   # define [Ws]::CountResident

$inv = [Globalization.CultureInfo]::InvariantCulture
$t0 = Get-Date
while (-not (Get-Process storm2 -ErrorAction SilentlyContinue) -and ((Get-Date) - $t0).TotalSeconds -lt $EsperaInicio) {
  Start-Sleep 1
}
$p = Get-Process storm2 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { 'storm2 nao iniciou'; exit 1 }
$cols = @('segundos') + (0..15 | ForEach-Object { 'g{0:X}' -f $_ }) + @('fisica', 'processo_ws', 'processo_privado')
($cols -join ',') | Out-File $Saida -Encoding ascii
while (-not $p.HasExited) {
  try {
    $vals = @([string]::Format($inv, '{0:F0}', ((Get-Date) - $p.StartTime).TotalSeconds))
    for ($g = 0; $g -lt 16; $g++) {
      $r = [Ws]::CountResident($p.Id, 0x100000000 + [int64]$g * 0x10000000, 0x10000000)
      $vals += [string]::Format($inv, '{0:F1}', $r / 1MB)
    }
    $vals += [string]::Format($inv, '{0:F1}', [Ws]::CountResident($p.Id, 0x200000000, 0x20000000) / 1MB)
    $p.Refresh()
    $vals += [string]::Format($inv, '{0:F1}', $p.WorkingSet64 / 1MB)
    $vals += [string]::Format($inv, '{0:F1}', $p.PrivateMemorySize64 / 1MB)
    ($vals -join ',') | Out-File $Saida -Append -Encoding ascii
  } catch { }
  Start-Sleep $Intervalo
  $p.Refresh()
}
"amostras em $Saida"
