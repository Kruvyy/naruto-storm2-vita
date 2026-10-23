# Sessao de perfil enquanto alguem joga: espera o storm2.exe (aberto por
# run_storm2_stats.ps1 -Nome <Nome>), e em ciclo, ate o processo terminar:
#   1. captura a janela (storm2\captures\<Nome>\cNNN_tSSSS.png, gitignored) para
#      saber depois em que tela o jogo estava;
#   2. amostra cada thread de -Threads por -Segundos com profile_guest_thread.ps1
#      (docs\logs\<Nome>\perfil_<thread>_cNNN.csv/.txt).
# Indice dos ciclos: docs\logs\<Nome>\sessao.csv.
#
# Uso: .\scripts\profile_session.ps1 -Nome batalha3 [-Threads F80000B0,'Main XThread'] [-Segundos 8]
param(
  [Parameter(Mandatory)] [string] $Nome,
  [string[]] $Threads = @('F80000B0', 'Main XThread'),
  [int] $Segundos = 8,
  [int] $EsperaInicio = 120
)
$root = Split-Path -Parent $PSScriptRoot
$run = (New-Item -ItemType Directory -Force (Join-Path $root "docs\logs\$Nome")).FullName
$cap = (New-Item -ItemType Directory -Force (Join-Path $root "storm2\captures\$Nome")).FullName
$prof = Join-Path $PSScriptRoot 'profile_guest_thread.ps1'

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class SessCap {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  public static string Shot(IntPtr hwnd, string path) {
    RECT r; if (!GetClientRect(hwnd, out r)) return "sem janela";
    int w = r.R - r.L, h = r.B - r.T; if (w <= 0 || h <= 0) return "minimizada";
    using (var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
      using (var g = Graphics.FromImage(bmp)) { IntPtr dc = g.GetHdc(); PrintWindow(hwnd, dc, 3); g.ReleaseHdc(dc); }
      bmp.Save(path, ImageFormat.Png);
    }
    return "ok";
  }
}
'@

$t0 = Get-Date
while (-not (Get-Process storm2 -ErrorAction SilentlyContinue) -and ((Get-Date) - $t0).TotalSeconds -lt $EsperaInicio) { Start-Sleep 1 }
$p = Get-Process storm2 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { 'storm2 nao iniciou'; exit 1 }
# thread_insns.csv (thread names) is written every 300 frames.
while (-not (Test-Path (Join-Path $run 'thread_insns.csv')) -and -not $p.HasExited) { Start-Sleep 2 }
'ciclo,segundos,captura,thread,amostras,cpu_pct,jogo_pct,top1,top1_pct_jogo,top5_pct_jogo' | Out-File (Join-Path $run 'sessao.csv') -Encoding ascii
$cycle = 0
while (-not $p.HasExited) {
  $cycle++
  $t = ((Get-Date) - $p.StartTime).TotalSeconds
  $p.Refresh()
  $png = Join-Path $cap ('c{0:D3}_t{1:D4}.png' -f $cycle, [int]$t)
  $shot = if ($p.MainWindowHandle -ne [IntPtr]::Zero) { [SessCap]::Shot($p.MainWindowHandle, $png) } else { 'sem janela' }
  foreach ($th in $Threads) {
    if ($p.HasExited) { break }
    $tag = $th -replace ' ', '_'
    $csv = Join-Path $run ('perfil_{0}_c{1:D3}.csv' -f $tag, $cycle)
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $prof -RunDir $run -Thread $th -Segundos $Segundos -Top 15 -Saida $csv 2>&1
    $out | Out-File ($csv -replace '\.csv$', '.txt') -Encoding utf8
    $samples = ($out | Select-String 'amostras em' | ForEach-Object { ($_ -split ':')[1].Trim().Split(' ')[0] }) | Select-Object -First 1
    $cpu = ($out | Select-String 'CPU da thread: ([0-9.]+)%' | ForEach-Object { $_.Matches[0].Groups[1].Value }) | Select-Object -First 1
    $game = ($out | Select-String 'jogo ([0-9.]+)%' | ForEach-Object { $_.Matches[0].Groups[1].Value }) | Select-Object -First 1
    $top1 = ''; $top1p = 0; $top5p = 0
    if (Test-Path $csv) {
      # (Measure-Object with a script block does not sum in PowerShell 5.1.)
      $rows = @(Import-Csv $csv | Where-Object { $_.simbolo -like 'sub_*' })
      $sum = 0; foreach ($r in $rows) { $sum += [int]$r.amostras }
      if ($sum) {
        $top1 = $rows[0].simbolo; $top1p = [math]::Round(100 * [int]$rows[0].amostras / $sum, 1)
        $s5 = 0; foreach ($r in ($rows | Select-Object -First 5)) { $s5 += [int]$r.amostras }
        $top5p = [math]::Round(100 * $s5 / $sum, 1)
      }
    }
    [string]::Format([Globalization.CultureInfo]::InvariantCulture, '{0},{1:F0},{2},{3},{4},{5},{6},{7},{8},{9}',
      $cycle, $t, $shot, $tag, $samples, $cpu, $game, $top1, $top1p, $top5p) |
      Out-File (Join-Path $run 'sessao.csv') -Append -Encoding ascii
  }
  $p.Refresh()
}
"sessao terminada: $cycle ciclos; indice em $run\sessao.csv; capturas em $cap"
