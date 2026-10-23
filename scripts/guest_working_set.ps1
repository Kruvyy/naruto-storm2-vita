# Mede as paginas do convidado REALMENTE residentes (tocadas) num processo do
# ReXGlue em execucao, via QueryWorkingSetEx sobre as areas mapeadas:
#   virtual  : 0x1_0000_0000 .. 0x1_A000_0000 (heaps virtuais do convidado)
#   fisica   : 0x2_0000_0000 .. 0x2_2000_0000 (512 MiB)
# (bases impressas no log: "Guest memory arena mapped: virtual base ..., physical base ...")
# Observacao: as visoes 0xA0/0xC0/0xE0 do convidado aliam a mesma memoria fisica;
# aqui conta-se a memoria fisica uma vez, pela base fisica.
param([Parameter(Mandatory)] [int] $ProcessId)

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Ws {
  [StructLayout(LayoutKind.Sequential)]
  public struct PSAPI_WORKING_SET_EX_INFORMATION { public IntPtr VirtualAddress; public UIntPtr VirtualAttributes; }
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  [DllImport("psapi.dll", SetLastError=true)] public static extern bool QueryWorkingSetEx(IntPtr h, [In, Out] PSAPI_WORKING_SET_EX_INFORMATION[] pv, int cb);
  public static long CountResident(int pid, long start, long size) {
    IntPtr h = OpenProcess(0x0400 | 0x0010, false, pid); // QUERY_INFORMATION | VM_READ
    if (h == IntPtr.Zero) throw new Exception("OpenProcess falhou");
    const int page = 4096, batch = 65536;
    long resident = 0;
    var buf = new PSAPI_WORKING_SET_EX_INFORMATION[batch];
    int sz = Marshal.SizeOf(typeof(PSAPI_WORKING_SET_EX_INFORMATION));
    for (long a = start; a < start + size; a += (long)page * batch) {
      int n = (int)Math.Min(batch, (start + size - a) / page);
      for (int i = 0; i < n; i++) { buf[i].VirtualAddress = new IntPtr(a + (long)i * page); buf[i].VirtualAttributes = UIntPtr.Zero; }
      if (!QueryWorkingSetEx(h, buf, n * sz)) throw new Exception("QueryWorkingSetEx falhou");
      for (int i = 0; i < n; i++) if ((buf[i].VirtualAttributes.ToUInt64() & 1) != 0) resident++;
    }
    CloseHandle(h);
    return resident * page;
  }
}
'@
# Virtual heaps only (guest 0x00000000-0x9FFFFFFF). Guest 0xA0000000-0xFFFFFFFF are
# views of physical memory and would count the same pages twice.
# 0xA0000000L: without the L suffix, PowerShell 5.1 parses it as a negative
# Int32 and the scan covered nothing (virtual always reported 0).
$v = [Ws]::CountResident($ProcessId, 0x100000000, 0xA0000000L)
$p = [Ws]::CountResident($ProcessId, 0x200000000, 0x20000000)
"residente virtual  : {0:N1} MiB" -f ($v / 1MB)
"residente fisica   : {0:N1} MiB" -f ($p / 1MB)
"residente total    : {0:N1} MiB" -f (($v + $p) / 1MB)
