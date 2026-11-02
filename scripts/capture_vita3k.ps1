# Captura a janela do Vita3K em execucao (PrintWindow, como capture_storm2.ps1)
# para ver o que o renderizador GLES mostra no emulador.
#
# Uso: .\scripts\capture_vita3k.ps1 -Saida storm2\captures\vita3k\shot.png
#      -Exe <caminho do Vita3K.exe>: so o processo desse executavel (outro Vita3K
#      aberto pelo usuario fica de fora).
param([Parameter(Mandatory)] [string] $Saida, [string] $Titulo = 'FPS', [string] $Exe = '')

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class V3kCap {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  // The game window (title containing "title"), else the largest visible one.
  public static IntPtr Find(uint pid, string title) {
    IntPtr best = IntPtr.Zero; long best_area = 0;
    EnumWindows((h, p) => {
      uint wp; GetWindowThreadProcessId(h, out wp);
      if (wp != pid || !IsWindowVisible(h)) return true;
      RECT r; if (!GetClientRect(h, out r)) return true;
      var t = new System.Text.StringBuilder(512); GetWindowText(h, t, 512);
      long a = (long)(r.R - r.L) * (r.B - r.T);
      if (title.Length > 0 && t.ToString().Contains(title)) a += 1L << 40;
      if (a > best_area) { best_area = a; best = h; }
      return true;
    }, IntPtr.Zero);
    return best;
  }
  public static string Shot(IntPtr hwnd, string path) {
    RECT r; if (!GetClientRect(hwnd, out r)) return "GetClientRect falhou";
    int w = r.R - r.L, h = r.B - r.T; if (w <= 0 || h <= 0) return "janela sem area";
    using (var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
      using (var g = Graphics.FromImage(bmp)) {
        IntPtr hdc = g.GetHdc();
        bool ok = PrintWindow(hwnd, hdc, 3);
        g.ReleaseHdc(hdc);
        if (!ok) return "PrintWindow falhou";
      }
      bmp.Save(path, ImageFormat.Png);
      return w + "x" + h;
    }
  }
}
'@

$p = Get-Process Vita3K -ErrorAction SilentlyContinue |
  Where-Object { -not $Exe -or ($_.Path -and $_.Path -ieq [IO.Path]::GetFullPath($Exe)) } |
  Select-Object -First 1
if (-not $p) { throw 'Vita3K nao esta rodando' }
$h = [V3kCap]::Find([uint32]$p.Id, $Titulo)
if ($h -eq [IntPtr]::Zero) { throw 'janela do Vita3K nao encontrada' }
$dir = Split-Path -Parent $Saida
if ($dir) { New-Item -ItemType Directory -Force $dir | Out-Null }
$full = [IO.Path]::GetFullPath($Saida)
"captura: " + [V3kCap]::Shot($h, $full) + " -> $full"
