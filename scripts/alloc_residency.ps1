# Residencia por alocacao do convidado: le as linhas "guest-alloc"/"guest-free"
# do log (--log_guest_allocations=true), mantem as alocacoes vivas e mede,
# com QueryWorkingSetEx no storm2.exe em execucao, quantas paginas de cada uma
# ja foram tocadas. Para alocacoes fisicas junta a visao usada pela CPU
# (0x1_0000_0000 + endereco) e a base fisica usada pela GPU (0x2_0000_0000 +
# endereco fisico; a visao 0xE0000000 tem deslocamento de 0x1000).
#
# Uso: .\scripts\alloc_residency.ps1 -Log docs\logs\X\storm2.log -Saida docs\logs\X\alloc_residency.csv
param([Parameter(Mandatory)] [string] $Log, [string] $Saida = '', [int] $Top = 15)

$p = Get-Process storm2 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { 'storm2 nao esta rodando'; exit 1 }

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class WsA {
  [StructLayout(LayoutKind.Sequential)]
  public struct PSAPI_WORKING_SET_EX_INFORMATION { public IntPtr VirtualAddress; public UIntPtr VirtualAttributes; }
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("psapi.dll", SetLastError=true)] static extern bool QueryWorkingSetEx(IntPtr h, [In, Out] PSAPI_WORKING_SET_EX_INFORMATION[] pv, int cb);
  static IntPtr h;
  public static void Open(int pid) { h = OpenProcess(0x0400 | 0x0010, false, pid); if (h == IntPtr.Zero) throw new Exception("OpenProcess"); }
  public static void Close() { CloseHandle(h); }
  // Resident flags for `pages` 4 KiB pages starting at host address `start`.
  public static bool[] Resident(long start, int pages) {
    var res = new bool[pages];
    const int batch = 65536;
    var buf = new PSAPI_WORKING_SET_EX_INFORMATION[batch];
    int sz = Marshal.SizeOf(typeof(PSAPI_WORKING_SET_EX_INFORMATION));
    for (int off = 0; off < pages; off += batch) {
      int n = Math.Min(batch, pages - off);
      for (int i = 0; i < n; i++) { buf[i].VirtualAddress = new IntPtr(start + (long)(off + i) * 4096); buf[i].VirtualAttributes = UIntPtr.Zero; }
      if (!QueryWorkingSetEx(h, buf, n * sz)) throw new Exception("QueryWorkingSetEx");
      for (int i = 0; i < n; i++) res[off + i] = (buf[i].VirtualAttributes.ToUInt64() & 1) != 0;
    }
    return res;
  }
}
'@

$live = [ordered]@{}
foreach ($l in Get-Content $Log) {
  if ($l -match 'guest-alloc (virtual|physical) addr=([0-9A-F]{8}) size=([0-9A-F]{8})') {
    $live[$Matches[2]] = [pscustomobject]@{ kind = $Matches[1]; addr = [Convert]::ToUInt32($Matches[2], 16); size = [Convert]::ToUInt32($Matches[3], 16) }
  } elseif ($l -match 'guest-free virtual addr=([0-9A-F]{8}) size=([0-9A-F]{8}) type=([0-9A-F]{8})') {
    if ([Convert]::ToUInt32($Matches[3], 16) -band 0x8000) { $live.Remove($Matches[1]) }  # MEM_RELEASE
  } elseif ($l -match 'guest-free physical addr=([0-9A-F]{8})') {
    $live.Remove($Matches[1])
  }
}

[WsA]::Open($p.Id)
$rows = foreach ($a in $live.Values) {
  $pages = [int][math]::Ceiling($a.size / 4096)
  $view = [WsA]::Resident(0x100000000L + $a.addr, $pages)
  $touched = 0
  if ($a.kind -eq 'physical') {
    $phys = ($a.addr -band 0x1FFFFFFF) + $(if ($a.addr -ge 0xE0000000) { 0x1000 } else { 0 })
    $base = [WsA]::Resident(0x200000000L + $phys, $pages)
    for ($i = 0; $i -lt $pages; $i++) { if ($view[$i] -or $base[$i]) { $touched++ } }
  } else {
    for ($i = 0; $i -lt $pages; $i++) { if ($view[$i]) { $touched++ } }
  }
  $band = ''
  if ($a.size -ge 16MB) {
    # Touched MiB per 8 MiB band, low to high address: shows whether the
    # game's allocator fills its pool from one end (a growable prefix).
    $bands = @()
    for ($b = 0; $b -lt $pages; $b += 2048) {
      $c = 0
      for ($i = $b; $i -lt [math]::Min($pages, $b + 2048); $i++) {
        if ($view[$i] -or ($a.kind -eq 'physical' -and $base[$i])) { $c++ }
      }
      $bands += [string][math]::Round($c * 4096 / 1MB, 1)
    }
    $band = $bands -join ' '
  }
  [pscustomobject]@{ kind = $a.kind; addr = '{0:X8}' -f $a.addr; size_mib = [math]::Round($a.size / 1MB, 2); touched_mib = [math]::Round($touched * 4096 / 1MB, 2); untouched_mib = [math]::Round(($a.size - $touched * 4096) / 1MB, 2); bands_8mib = $band }
}
[WsA]::Close()

$inv = [Globalization.CultureInfo]::InvariantCulture
foreach ($k in 'virtual', 'physical') {
  $r = @($rows | Where-Object kind -eq $k)
  $s = 0.0; $t = 0.0; foreach ($x in $r) { $s += $x.size_mib; $t += $x.touched_mib }
  [string]::Format($inv, '{0}: {1} alocacoes vivas, {2:F1} MiB alocados, {3:F1} MiB tocados', $k, $r.Count, $s, $t)
}
"--- maiores areas nao tocadas:"
$rows | Sort-Object untouched_mib -Descending | Select-Object -First $Top kind, addr, size_mib, touched_mib, untouched_mib | Format-Table -AutoSize | Out-String -Width 160
"--- alocacoes >= 16 MiB: MiB tocados por faixa de 8 MiB (endereco crescente):"
$rows | Where-Object { $_.bands_8mib } | ForEach-Object { '{0} {1} ({2} MiB): {3}' -f $_.kind, $_.addr, $_.size_mib, $_.bands_8mib }
"--- distribuicao por tamanho (alocacoes vivas):"
$bins = @(@(0, 0.25), @(0.25, 1), @(1, 4), @(4, 16), @(16, 1e9))
foreach ($b in $bins) {
  $r = @($rows | Where-Object { $_.size_mib -ge $b[0] -and $_.size_mib -lt $b[1] })
  $s = 0.0; $t = 0.0; foreach ($x in $r) { $s += $x.size_mib; $t += $x.touched_mib }
  [string]::Format($inv, '  {0,6} - {1,-6} MiB: {2,5} alocacoes, {3,7:F1} MiB alocados, {4,7:F1} MiB tocados', $b[0], $(if ($b[1] -ge 1e9) { 'inf' } else { $b[1] }), $r.Count, $s, $t)
}
if ($Saida) { $rows | Export-Csv $Saida -NoTypeInformation -Encoding ascii }
