# Perfil por amostragem de uma thread do storm2.exe em execucao.
#
# A cada ~IntervaloMs suspende a thread, le o RIP (GetThreadContext) e a
# retoma. Cada amostra vira:
#   - funcao do jogo (sub_XXXXXXXX), quando o RIP cai no storm2.exe. O mapa
#     endereco host -> funcao vem da tabela de despacho do proprio runtime
#     (base + REX_IMAGE_BASE + REX_IMAGE_SIZE + (guest - REX_CODE_BASE) * 2,
#     um ponteiro de 8 bytes por palavra de codigo), lida da memoria do processo;
#   - export mais proximo do rexruntime.dll / ntdll.dll / KernelBase.dll;
#   - ou o nome do modulo.
# Tambem mede o tempo de CPU da thread no periodo (ocupada x esperando).
#
# Uso:
#   .\scripts\profile_guest_thread.ps1 -RunDir docs\logs\X [-Thread F8000068] [-Segundos 20]
#   (a thread e achada pelo nome em <RunDir>\thread_insns.csv; ou -NativeId N)
param(
  [string] $RunDir,
  [string] $Thread = 'F8000068',
  [int] $NativeId = 0,
  [int] $Segundos = 20,
  [int] $IntervaloMs = 1,
  [int] $Top = 25,
  [string] $Saida = ''
)
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'scripts\env.ps1') | Out-Null
$p = Get-Process storm2 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { 'storm2 nao esta rodando'; exit 1 }
if (-not $NativeId) {
  $row = Import-Csv (Join-Path $RunDir 'thread_insns.csv') | Where-Object { $_.guest_thread_name -like "*$Thread*" } | Select-Object -First 1
  if (-not $row) { "thread $Thread nao encontrada em thread_insns.csv"; exit 1 }
  $NativeId = [int]$row.native_thread
}

# Generated code layout (storm2_pch.h).
$pch = Get-Content (Join-Path $root 'storm2\generated\default\storm2_pch.h') -Raw
function Get-Def($n) { [Convert]::ToUInt64(([regex]::Match($pch, "define $n 0x([0-9A-Fa-f]+)")).Groups[1].Value, 16) }
$imageBase = Get-Def 'REX_IMAGE_BASE'; $imageSize = Get-Def 'REX_IMAGE_SIZE'
$codeBase = Get-Def 'REX_CODE_BASE'; $codeSize = Get-Def 'REX_CODE_SIZE'
$init = Get-Content (Join-Path $root 'storm2\generated\default\storm2_init.cpp') -Raw
$guestFuncs = [uint32[]]([regex]::Matches($init, '\{ ?0x([0-9A-Fa-f]{8}), ?sub_') | ForEach-Object { [Convert]::ToUInt32($_.Groups[1].Value, 16) })

# Export tables for named runtime/OS symbols.
$mods = @{}
foreach ($m in $p.Modules) { $mods[$m.ModuleName.ToLower()] = $m }
$symNames = New-Object System.Collections.Generic.List[string]
$symAddrs = New-Object System.Collections.Generic.List[uint64]
foreach ($name in 'rexruntime.dll', 'ntdll.dll', 'kernelbase.dll') {
  $m = $mods[$name]; if (-not $m) { continue }
  $out = llvm-readobj --coff-exports $m.FileName 2>$null
  $cur = $null
  foreach ($l in $out) {
    if ($l -match '^\s*Name:\s*(\S+)') { $cur = $Matches[1] }
    elseif ($l -match '^\s*RVA:\s*0x([0-9A-Fa-f]+)' -and $cur) {
      $symAddrs.Add([uint64]$m.BaseAddress.ToInt64() + [Convert]::ToUInt64($Matches[1], 16)); $symNames.Add("${name}!$cur"); $cur = $null
    }
  }
}
$modNames = @($p.Modules | ForEach-Object { $_.ModuleName })
$modBases = [uint64[]]@($p.Modules | ForEach-Object { [uint64]$_.BaseAddress.ToInt64() })
$modSizes = [uint64[]]@($p.Modules | ForEach-Object { [uint64]$_.ModuleMemorySize })

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
public static class Prof {
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

  // Host function pointer for each guest function, from the dispatch table.
  public static ulong[] ReadDispatch(int pid, ulong tableHost, ulong codeBase, ulong codeSize, uint[] funcs) {
    IntPtr h = OpenProcess(0x0010 | 0x0400, false, pid);
    if (h == IntPtr.Zero) throw new Exception("OpenProcess falhou");
    ulong bytes = codeSize * 2;
    var buf = new byte[bytes];
    const int chunk = 1 << 20;
    for (ulong off = 0; off < bytes; off += chunk) {
      int n = (int)Math.Min((ulong)chunk, bytes - off);
      var tmp = new byte[n]; IntPtr rd;
      if (!ReadProcessMemory(h, new IntPtr((long)(tableHost + off)), tmp, new IntPtr(n), out rd)) continue;
      Buffer.BlockCopy(tmp, 0, buf, (int)off, n);
    }
    CloseHandle(h);
    var res = new ulong[funcs.Length];
    for (int i = 0; i < funcs.Length; i++) {
      ulong idx = ((ulong)funcs[i] - codeBase) * 2;
      res[i] = idx + 8 <= bytes ? BitConverter.ToUInt64(buf, (int)idx) : 0;
    }
    return res;
  }

  // Returns sampled RIPs; cpu[0] = thread CPU time (100 ns units) during sampling.
  // callers (same length): when RIP is outside [exeLo, exeHi), the first stack
  // word inside the exe (heuristic scan of 4 KB from RSP), i.e. the nearest
  // game-code caller of the runtime/OS function; 0 otherwise.
  public static List<ulong> Callers = new List<ulong>();
  public static ulong[] Sample(int pid, int tid, int seconds, int intervalMs, long[] cpu, ulong exeLo, ulong exeHi) {
    IntPtr h = OpenThread(0x0002 | 0x0008 | 0x0040, false, tid);
    if (h == IntPtr.Zero) throw new Exception("OpenThread falhou");
    IntPtr hp = OpenProcess(0x0010 | 0x0400, false, pid);
    IntPtr raw = Marshal.AllocHGlobal(1232 + 16);
    IntPtr ctx = new IntPtr((raw.ToInt64() + 15) & ~15L);
    var rips = new List<ulong>();
    Callers.Clear();
    var stack = new byte[4096];
    long c, e, k0, u0, k1, u1;
    GetThreadTimes(h, out c, out e, out k0, out u0);
    timeBeginPeriod(1);
    var end = DateTime.UtcNow.AddSeconds(seconds);
    while (DateTime.UtcNow < end) {
      if (SuspendThread(h) != 0xFFFFFFFF) {
        for (int i = 0; i < 1232; i += 8) Marshal.WriteInt64(ctx, i, 0);
        Marshal.WriteInt32(ctx, 0x30, 0x00100001);  // CONTEXT_AMD64 | CONTEXT_CONTROL
        if (GetThreadContext(h, ctx)) {
          ulong rip = (ulong)Marshal.ReadInt64(ctx, 0xF8);   // Rip
          ulong rsp = (ulong)Marshal.ReadInt64(ctx, 0x98);   // Rsp
          ulong caller = 0;
          if (rip < exeLo || rip >= exeHi) {
            IntPtr rd;
            if (ReadProcessMemory(hp, new IntPtr((long)rsp), stack, new IntPtr(stack.Length), out rd)) {
              for (int o = 0; o + 8 <= rd.ToInt64(); o += 8) {
                ulong v = BitConverter.ToUInt64(stack, o);
                if (v >= exeLo && v < exeHi) { caller = v; break; }
              }
            }
          }
          rips.Add(rip);
          Callers.Add(caller);
        }
        ResumeThread(h);
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

$tableHost = 0x100000000 + $imageBase + $imageSize
$hostPtrs = [Prof]::ReadDispatch($p.Id, $tableHost, $codeBase, $codeSize, $guestFuncs)
$pairs = for ($i = 0; $i -lt $guestFuncs.Length; $i++) { if ($hostPtrs[$i]) { [pscustomobject]@{ H = $hostPtrs[$i]; G = $guestFuncs[$i] } } }
$pairs = @($pairs | Sort-Object H)
$fH = [uint64[]]@($pairs | ForEach-Object { $_.H }); $fG = [uint32[]]@($pairs | ForEach-Object { $_.G })
$order = [uint64[]]$symAddrs.ToArray(); $names = $symNames.ToArray(); [Array]::Sort($order, $names)
"funcoes mapeadas pela tabela de despacho: $($fH.Length) de $($guestFuncs.Length)"

$exe = $mods['storm2.exe']; $exeLo = [uint64]$exe.BaseAddress.ToInt64(); $exeHi = $exeLo + [uint64]$exe.ModuleMemorySize
$cpu = New-Object long[] 2
$t0 = Get-Date
$rips = [Prof]::Sample($p.Id, $NativeId, $Segundos, $IntervaloMs, $cpu, $exeLo, $exeHi)
$callers = [Prof]::Callers.ToArray()
$wall = ((Get-Date) - $t0).TotalSeconds
function Find-Floor([uint64[]] $arr, [uint64] $x) {
  $lo = 0; $hi = $arr.Length - 1; $ans = -1
  while ($lo -le $hi) { $mid = [int](($lo + $hi) / 2); if ($arr[$mid] -le $x) { $ans = $mid; $lo = $mid + 1 } else { $hi = $mid - 1 } }
  return $ans
}
$counts = @{}; $cls = @{ jogo = 0; runtime = 0; windows = 0; outro = 0 }; $byCaller = @{}
for ($si = 0; $si -lt $rips.Length; $si++) {
  $r = $rips[$si]
  $key = $null
  if ($r -ge $exeLo -and $r -lt $exeHi) {
    $i = Find-Floor $fH $r
    if ($i -ge 0) { $key = 'sub_{0:X8}' -f $fG[$i] } else { $key = 'storm2.exe (fora das funcoes)' }
    $cls.jogo++
  } else {
    $j = Find-Floor $order $r
    $mi = -1; for ($k = 0; $k -lt $modBases.Length; $k++) { if ($r -ge $modBases[$k] -and $r -lt $modBases[$k] + $modSizes[$k]) { $mi = $k; break } }
    $mod = if ($mi -ge 0) { $modNames[$mi].ToLower() } else { '?' }
    if ($j -ge 0 -and $names[$j].StartsWith("$mod!")) { $key = $names[$j] } else { $key = $mod }
    if ($mod -eq 'rexruntime.dll') { $cls.runtime++ } elseif ($mod -in 'ntdll.dll', 'kernelbase.dll', 'kernel32.dll', 'win32u.dll') { $cls.windows++ } else { $cls.outro++ }
    # Nearest game-code caller found on the stack.
    $c = $callers[$si]
    $ck = '(sem chamador do jogo na pilha)'
    if ($c) { $ci = Find-Floor $fH $c; if ($ci -ge 0) { $ck = 'sub_{0:X8}' -f $fG[$ci] } }
    $byCaller[$ck] = 1 + $(if ($byCaller.ContainsKey($ck)) { $byCaller[$ck] } else { 0 })
  }
  $counts[$key] = 1 + $(if ($counts.ContainsKey($key)) { $counts[$key] } else { 0 })
}
$n = [math]::Max(1, $rips.Length)
$inv = [Globalization.CultureInfo]::InvariantCulture
"thread nativa $NativeId ($Thread): {0} amostras em {1:F1} s" -f $rips.Length, $wall
[string]::Format($inv, 'CPU da thread: {0:F1}% de um nucleo (kernel {1:F1}%)', 100.0 * $cpu[0] / 1e7 / $wall, 100.0 * $cpu[1] / 1e7 / $wall)
[string]::Format($inv, 'amostras: jogo {0:F1}% | runtime {1:F1}% | windows {2:F1}% | outro {3:F1}%', 100.0 * $cls.jogo / $n, 100.0 * $cls.runtime / $n, 100.0 * $cls.windows / $n, 100.0 * $cls.outro / $n)
$lines = $counts.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First $Top | ForEach-Object { [string]::Format($inv, '{0,6:F2}%  {1,7}  {2}', 100.0 * $_.Value / $n, $_.Value, $_.Key) }
$lines
'--- amostras fora do jogo (runtime/Windows), pela funcao do jogo que chamou (primeiro retorno na pilha):'
$byCaller.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 10 | ForEach-Object { [string]::Format($inv, '{0,6:F2}%  {1,7}  {2}', 100.0 * $_.Value / $n, $_.Value, $_.Key) }
if ($Saida) {
  $all = $counts.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { '{0},{1}' -f $_.Key, $_.Value }
  (@('simbolo,amostras') + $all) | Out-File $Saida -Encoding ascii
}
