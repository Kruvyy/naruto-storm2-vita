# Roda o Storm 2 recompilado (x64) e captura a janela a cada -Intervalo
# segundos (PrintWindow com PW_RENDERFULLCONTENT, funciona com D3D12 mesmo
# com a janela atras de outras). Para cada captura imprime o brilho medio e a
# fracao de pixels nao pretos, para medir "tela preta" com numero.
#
# As imagens sao do jogo (material proprietario): ficam em storm2\captures\,
# que esta no .gitignore. O log fica em docs\logs\<Nome>\storm2.log.
#
# Uso:
#   .\scripts\capture_storm2.ps1 -Nome D20_cap -Segundos 180 -Intervalo 10
#   .\scripts\capture_storm2.ps1 -Nome D21 -Extra '--log_level=debug'
# -Segundos 0 = espera o processo terminar (nao encerra o jogo).
# -Build softmmu = memoria por software (como no Vita), so com --gpu_backend=null.
param(
  [Parameter(Mandatory)] [string] $Nome,
  [int] $Segundos = 180,
  [int] $Intervalo = 10,
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
$cap = (New-Item -ItemType Directory -Force (Join-Path $root "storm2\captures\$Nome")).FullName

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class Cap {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  // Returns "mean_luma nonblack_fraction width height" and saves a PNG.
  public static string Shot(IntPtr hwnd, string path) {
    RECT r; if (!GetClientRect(hwnd, out r)) return "GetClientRect falhou";
    int w = r.R - r.L, h = r.B - r.T; if (w <= 0 || h <= 0) return "janela sem area";
    using (var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
      using (var g = Graphics.FromImage(bmp)) {
        IntPtr hdc = g.GetHdc();
        bool ok = PrintWindow(hwnd, hdc, 3); // PW_CLIENTONLY | PW_RENDERFULLCONTENT
        g.ReleaseHdc(hdc);
        if (!ok) return "PrintWindow falhou";
      }
      var d = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
      var px = new byte[d.Stride * h]; Marshal.Copy(d.Scan0, px, 0, px.Length); bmp.UnlockBits(d);
      double sum = 0; long nonblack = 0;
      for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int o = y * d.Stride + x * 4;
        double l = 0.114 * px[o] + 0.587 * px[o + 1] + 0.299 * px[o + 2];
        sum += l; if (l > 8) nonblack++;
      }
      bmp.Save(path, ImageFormat.Png);
      return string.Format(System.Globalization.CultureInfo.InvariantCulture,
        "brilho_medio={0:F2} nao_preto={1:P2} {2}x{3}", sum / ((double)w * h), (double)nonblack / ((double)w * h), w, h);
    }
  }
}
'@

$exe = Join-Path $bd 'storm2.exe'
$game = Join-Path $root 'storm2\game'   # copia dos dados; o original nunca e usado
$a = @("--game_data_root=`"$game`"", '--gpu_plugin=xenos', "--log_file=`"$run\storm2.log`"",
       '--fullscreen=false', '--window_width=1280', '--window_height=720') + $Extra
$p = Start-Process -FilePath $exe -ArgumentList $a -WorkingDirectory $run -PassThru
$t0 = Get-Date; $next = $Intervalo; $i = 0
while (-not $p.HasExited -and ($Segundos -le 0 -or ((Get-Date) - $t0).TotalSeconds -lt $Segundos)) {
  Start-Sleep -Milliseconds 500
  $t = ((Get-Date) - $t0).TotalSeconds
  if ($t -ge $next) {
    $p.Refresh(); $h = $p.MainWindowHandle
    if ($h -ne [IntPtr]::Zero) {
      $f = Join-Path $cap ('t{0:D4}.png' -f [int]$t)
      "t={0,4:N0} s  {1}" -f $t, [Cap]::Shot($h, $f)
    } else { "t={0,4:N0} s  sem janela" -f $t }
    $next += $Intervalo; $i++
  }
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Start-Sleep 2; "encerrado pelo script apos $Segundos s" }
else { "o processo terminou sozinho apos {0:N0} s, codigo 0x{1:X8}" -f ((Get-Date) - $t0).TotalSeconds, $p.ExitCode }
"log: $run\storm2.log"; "capturas: $cap"
