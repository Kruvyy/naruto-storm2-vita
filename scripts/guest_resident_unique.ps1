# Memoria do convidado realmente residente, sem contar aliases duas vezes.
#
# O runtime mapeia um unico objeto de memoria em varias visoes
# (src/system/xmemory.cpp, map_info). Aliases: 0x90000000 = 0x80000000;
# 0x7F000000, 0xA0000000, 0xC0000000, 0xE0000000 e a base fisica = memoria
# fisica. QueryWorkingSetEx informa residencia por endereco virtual (cada visao
# tem seus PTEs), entao aqui cada pagina residente de cada visao e convertida
# para o deslocamento no objeto de memoria e contada uma vez (uniao).
#
# Uso: .\scripts\guest_resident_unique.ps1 -ProcessId N
param([Parameter(Mandatory)] [int] $ProcessId)

Add-Type -TypeDefinition @'
using System;
using System.Collections;
using System.Runtime.InteropServices;
public static class WsU {
  [StructLayout(LayoutKind.Sequential)]
  public struct PSAPI_WORKING_SET_EX_INFORMATION { public IntPtr VirtualAddress; public UIntPtr VirtualAttributes; }
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("psapi.dll", SetLastError=true)] static extern bool QueryWorkingSetEx(IntPtr h, [In, Out] PSAPI_WORKING_SET_EX_INFORMATION[] pv, int cb);
  const long Page = 4096;
  // views: guest start, guest end (inclusive), offset in the memory object.
  static readonly long[,] Views = {
    {0x00000000L, 0x3FFFFFFFL, 0x000000000L},
    {0x40000000L, 0x7EFFFFFFL, 0x040000000L},
    {0x7F000000L, 0x7FFFFFFFL, 0x100000000L},
    {0x80000000L, 0x8FFFFFFFL, 0x080000000L},
    {0x90000000L, 0x9FFFFFFFL, 0x080000000L},
    {0xA0000000L, 0xBFFFFFFFL, 0x100000000L},
    {0xC0000000L, 0xDFFFFFFFL, 0x100000000L},
    {0xE0000000L, 0xFFFFFFFFL, 0x100000000L},
    {0x100000000L, 0x11FFFFFFFL, 0x100000000L},
  };
  // Returns {virtual_object_bytes, physical_object_bytes, sum_of_views_bytes}.
  public static long[] Measure(int pid, long hostBase) {
    IntPtr h = OpenProcess(0x0400 | 0x0010, false, pid);
    if (h == IntPtr.Zero) throw new Exception("OpenProcess falhou");
    var bits = new BitArray((int)(0x120000000L / Page));
    const int batch = 65536;
    var buf = new PSAPI_WORKING_SET_EX_INFORMATION[batch];
    int sz = Marshal.SizeOf(typeof(PSAPI_WORKING_SET_EX_INFORMATION));
    long viewSum = 0;
    for (int v = 0; v < Views.GetLength(0); v++) {
      long start = Views[v, 0], end = Views[v, 1] + 1, off = Views[v, 2];
      for (long a = start; a < end; a += Page * batch) {
        int n = (int)Math.Min(batch, (end - a) / Page);
        for (int i = 0; i < n; i++) { buf[i].VirtualAddress = new IntPtr(hostBase + a + i * Page); buf[i].VirtualAttributes = UIntPtr.Zero; }
        if (!QueryWorkingSetEx(h, buf, n * sz)) throw new Exception("QueryWorkingSetEx falhou");
        for (int i = 0; i < n; i++) {
          if ((buf[i].VirtualAttributes.ToUInt64() & 1) != 0) {
            viewSum++;
            bits[(int)((off + (a - start) + i * Page) / Page)] = true;
          }
        }
      }
    }
    CloseHandle(h);
    long virt = 0, phys = 0;
    int physFirst = (int)(0x100000000L / Page);
    for (int i = 0; i < bits.Length; i++) if (bits[i]) { if (i < physFirst) virt++; else phys++; }
    return new long[] { virt * Page, phys * Page, viewSum * Page };
  }
}
'@
$r = [WsU]::Measure($ProcessId, 0x100000000L)
$inv = [Globalization.CultureInfo]::InvariantCulture
[string]::Format($inv, 'unico virtual (heaps + xex): {0:F1} MiB', $r[0] / 1MB)
[string]::Format($inv, 'unico fisico: {0:F1} MiB', $r[1] / 1MB)
[string]::Format($inv, 'unico total: {0:F1} MiB', ($r[0] + $r[1]) / 1MB)
[string]::Format($inv, 'soma das visoes (conta aliases): {0:F1} MiB', $r[2] / 1MB)
