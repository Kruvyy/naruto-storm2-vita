# Perfil por amostragem da thread de um modulo do host (por padrao o backend
# de GPU, rexgpu-xenos.dll) no storm2.exe em execucao.
#
# A cada ~IntervaloMs suspende a thread, le o RIP (GetThreadContext) e a
# retoma. O RIP vira o nome da funcao pelo arquivo .map do modulo (link com
# -Wl,/MAP). Amostras fora do modulo (driver OpenGL, xxhash, CRT, Windows)
# contam pelo nome do modulo e tambem pela funcao do modulo que chamou
# (primeiro endereco de retorno na pilha). Sem -NativeId, a thread escolhida e
# a que mais cai dentro do modulo numa amostragem curta de todas as threads.
#
# Uso:
#   .\scripts\profile_host_thread.ps1 [-Segundos 20] [-Modulo rexgpu-xenos.dll]
#       [-Map rexglue\out\win-amd64\Release\rexgpu-xenos.map] [-Top 30] [-Saida arq.csv]
param(
  [int] $Segundos = 20,
  [int] $IntervaloMs = 1,
  [int] $NativeId = 0,
  [string] $Modulo = 'rexgpu-xenos.dll',
  [string] $Map = '',
  [int] $Top = 30,
  [string] $Saida = ''
)
$root = Split-Path -Parent $PSScriptRoot
if (-not $Map) { $Map = Join-Path $root 'rexglue\out\win-amd64\Release\rexgpu-xenos.map' }
$p = Get-Process storm2 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { 'storm2 nao esta rodando'; exit 1 }
$mod = $p.Modules | Where-Object { $_.ModuleName -ieq $Modulo } | Select-Object -First 1
if (-not $mod) { "modulo $Modulo nao carregado"; exit 1 }
$modLo = [uint64]$mod.BaseAddress.ToInt64(); $modHi = $modLo + [uint64]$mod.ModuleMemorySize

# Map file lines: "0001:00080560  ?Name@Class@ns@@...  0000000180081560  obj"
$text = Get-Content $Map
$pref = [uint64]0x180000000
foreach ($l in $text) {
  if ($l -match 'Preferred load address is ([0-9a-fA-F]+)') { $pref = [Convert]::ToUInt64($Matches[1], 16); break }
}
$rvas = New-Object System.Collections.Generic.List[uint64]
$names = New-Object System.Collections.Generic.List[string]
foreach ($l in $text) {
  if ($l -match '^\s*000[1-9]:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s') {
    $va = [Convert]::ToUInt64($Matches[2], 16)
    if ($va -lt $pref) { continue }
    $nm = $Matches[1]
    # ?Func@Class@ns@...@@... -> Class::Func (enough to read the profile).
    if ($nm -match '^\?([^@?]+)@([^@?]+)@') { $nm = "$($Matches[2])::$($Matches[1])" }
    $rvas.Add($va - $pref); $names.Add($nm)
  }
}
$rvaArr = $rvas.ToArray(); $nameArr = $names.ToArray(); [Array]::Sort($rvaArr, $nameArr)
"simbolos do .map: $($rvaArr.Length)"

$modNames = @($p.Modules | ForEach-Object { $_.ModuleName })
$modBases = [uint64[]]@($p.Modules | ForEach-Object { [uint64]$_.BaseAddress.ToInt64() })
$modSizes = [uint64[]]@($p.Modules | ForEach-Object { [uint64]$_.ModuleMemorySize })

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
public static class HostProf {
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenThread(uint a, bool i, int tid);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll", SetLastError=true)] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);
  [DllImport("kernel32.dll")] static extern uint SuspendThread(IntPtr h);
  [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr h);
  [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
  [DllImport("kernel32.dll")] static extern bool GetThreadTimes(IntPtr h, out long c, out long e, out long k, out long u);
  [DllImport("winmm.dll")] static extern uint timeBeginPeriod(uint p);
  [DllImport("winmm.dll")] static extern uint timeEndPeriod(uint p);

  static bool ReadRip(IntPtr h, IntPtr ctx, out ulong rip, out ulong rsp) {
    rip = 0; rsp = 0;
    if (SuspendThread(h) == 0xFFFFFFFF) return false;
    for (int i = 0; i < 1232; i += 8) Marshal.WriteInt64(ctx, i, 0);
    Marshal.WriteInt32(ctx, 0x30, 0x00100001);  // CONTEXT_AMD64 | CONTEXT_CONTROL
    bool ok = GetThreadContext(h, ctx);
    if (ok) { rip = (ulong)Marshal.ReadInt64(ctx, 0xF8); rsp = (ulong)Marshal.ReadInt64(ctx, 0x98); }
    ResumeThread(h);
    return ok;
  }

  // The thread whose RIP falls in [lo, hi) most often over a short round-robin.
  public static int PickThread(int pid, ulong lo, ulong hi, int rounds) {
    var proc = Process.GetProcessById(pid);
    var ids = new List<int>();
    foreach (ProcessThread t in proc.Threads) ids.Add(t.Id);
    var hits = new Dictionary<int, int>();
    IntPtr raw = Marshal.AllocHGlobal(1232 + 16);
    IntPtr ctx = new IntPtr((raw.ToInt64() + 15) & ~15L);
    for (int r = 0; r < rounds; r++) {
      foreach (int id in ids) {
        IntPtr h = OpenThread(0x0002 | 0x0008, false, id);
        if (h == IntPtr.Zero) continue;
        ulong rip, rsp;
        if (ReadRip(h, ctx, out rip, out rsp) && rip >= lo && rip < hi) {
          int v; hits.TryGetValue(id, out v); hits[id] = v + 1;
        }
        CloseHandle(h);
      }
      Thread.Sleep(5);
    }
    Marshal.FreeHGlobal(raw);
    int best = 0, bestHits = -1;
    foreach (var kv in hits) if (kv.Value > bestHits) { best = kv.Key; bestHits = kv.Value; }
    return best;
  }

  public static List<ulong> Callers = new List<ulong>();
  // Sampled RIPs; cpu[0] = thread CPU time (100 ns units) during sampling.
  // Callers[i]: when RIP is outside [lo, hi), the first stack word inside it.
  public static ulong[] Sample(int pid, int tid, int seconds, int intervalMs, long[] cpu, ulong lo, ulong hi) {
    IntPtr h = OpenThread(0x0002 | 0x0008 | 0x0040, false, tid);
    if (h == IntPtr.Zero) throw new Exception("OpenThread falhou");
    IntPtr hp = OpenProcess(0x0010 | 0x0400, false, pid);
    IntPtr raw = Marshal.AllocHGlobal(1232 + 16);
    IntPtr ctx = new IntPtr((raw.ToInt64() + 15) & ~15L);
    var rips = new List<ulong>();
    Callers.Clear();
    var stack = new byte[8192];
    long c, e, k0, u0, k1, u1;
    GetThreadTimes(h, out c, out e, out k0, out u0);
    timeBeginPeriod(1);
    var end = DateTime.UtcNow.AddSeconds(seconds);
    while (DateTime.UtcNow < end) {
      ulong rip, rsp;
      if (ReadRip(h, ctx, out rip, out rsp)) {
        ulong caller = 0;
        if (rip < lo || rip >= hi) {
          IntPtr rd;
          if (ReadProcessMemory(hp, new IntPtr((long)rsp), stack, new IntPtr(stack.Length), out rd)) {
            for (int o = 0; o + 8 <= rd.ToInt64(); o += 8) {
              ulong v = BitConverter.ToUInt64(stack, o);
              if (v >= lo && v < hi) { caller = v; break; }
            }
          }
        }
        rips.Add(rip);
        Callers.Add(caller);
      }
      Thread.Sleep(intervalMs);
    }
    timeEndPeriod(1);
    GetThreadTimes(h, out c, out e, out k1, out u1);
    cpu[0] = (k1 - k0) + (u1 - u0);
    cpu[1] = (k1 - k0);
    Marshal.FreeHGlobal(raw);
    CloseHandle(hp);
    CloseHandle(h);
    return rips.ToArray();
  }
}
'@

if (-not $NativeId) {
  $NativeId = [HostProf]::PickThread($p.Id, $modLo, $modHi, 60)
  if (-not $NativeId) { "nenhuma thread dentro de $Modulo"; exit 1 }
}
$cpu = New-Object long[] 2
$t0 = Get-Date
$rips = [HostProf]::Sample($p.Id, $NativeId, $Segundos, $IntervaloMs, $cpu, $modLo, $modHi)
$callers = [HostProf]::Callers.ToArray()
$wall = ((Get-Date) - $t0).TotalSeconds
function Find-Floor([uint64[]] $arr, [uint64] $x) {
  $lo = 0; $hi = $arr.Length - 1; $ans = -1
  while ($lo -le $hi) {
    $mid = [int](($lo + $hi) / 2)
    if ($arr[$mid] -le $x) { $ans = $mid; $lo = $mid + 1 } else { $hi = $mid - 1 }
  }
  return $ans
}
function Get-SymbolName([uint64] $addr) {
  $i = Find-Floor $rvaArr ($addr - $modLo)
  if ($i -ge 0) { return $nameArr[$i] } else { return "$Modulo (?)" }
}
function Add-Count($table, $key) { $table[$key] = 1 + $(if ($table.ContainsKey($key)) { $table[$key] } else { 0 }) }
$self = @{}; $viaCaller = @{}; $byMod = @{}
for ($si = 0; $si -lt $rips.Length; $si++) {
  $r = $rips[$si]
  if ($r -ge $modLo -and $r -lt $modHi) {
    Add-Count $self (Get-SymbolName $r)
    $m = $Modulo
  } else {
    $mi = -1
    for ($j = 0; $j -lt $modBases.Length; $j++) { if ($r -ge $modBases[$j] -and $r -lt $modBases[$j] + $modSizes[$j]) { $mi = $j; break } }
    $m = if ($mi -ge 0) { $modNames[$mi].ToLower() } else { '?' }
    Add-Count $self "[$m]"
    if ($callers[$si]) { Add-Count $viaCaller ((Get-SymbolName $callers[$si]) + " -> [$m]") }
  }
  Add-Count $byMod $m
}
$n = [math]::Max(1, $rips.Length)
$inv = [Globalization.CultureInfo]::InvariantCulture
"thread nativa {0}: {1} amostras em {2:F1} s" -f $NativeId, $rips.Length, $wall
[string]::Format($inv, 'CPU da thread: {0:F1}% de um nucleo (kernel {1:F1}%)', 100.0 * $cpu[0] / 1e7 / $wall, 100.0 * $cpu[1] / 1e7 / $wall)
'--- por modulo:'
$byMod.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 8 | ForEach-Object { [string]::Format($inv, '{0,6:F2}%  {1}', 100.0 * $_.Value / $n, $_.Key) }
'--- por funcao (amostra no proprio codigo; [modulo] = fora do backend):'
$self.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First $Top | ForEach-Object { [string]::Format($inv, '{0,6:F2}%  {1,6}  {2}', 100.0 * $_.Value / $n, $_.Value, $_.Key) }
'--- amostras fora do backend, pela funcao do backend que chamou:'
$viaCaller.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 15 | ForEach-Object { [string]::Format($inv, '{0,6:F2}%  {1,6}  {2}', 100.0 * $_.Value / $n, $_.Value, $_.Key) }
if ($Saida) {
  $all = $self.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { '"{0}",{1}' -f $_.Key, $_.Value }
  (@('simbolo,amostras') + $all) | Out-File $Saida -Encoding ascii
}
