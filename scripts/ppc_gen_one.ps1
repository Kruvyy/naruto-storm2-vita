# Monta um .s de teste PPC e gera o C++ com um rexglue.exe escolhido (sem CMake).
# Mesmas flags de rexglue/cmake/ppc_test_pipeline.cmake.
# Uso: .\scripts\ppc_gen_one.ps1 -Asm tests\ppc\asm\instr_x.s -Rexglue <rexglue.exe> -Out <dir>
param(
  [Parameter(Mandatory)] [string] $Asm,
  [Parameter(Mandatory)] [string] $Rexglue,
  [Parameter(Mandatory)] [string] $Out
)
$root = Split-Path -Parent $PSScriptRoot
$bu = Join-Path $root 'rexglue\tools\binutils'

function To-Cyg([string] $p) {
  $p = [System.IO.Path]::GetFullPath($p)
  $drive = $p.Substring(0, 1).ToLower()
  return '/cygdrive/' + $drive + ($p.Substring(2) -replace '\\', '/')
}

$asmAbs = (Resolve-Path $Asm).Path
$stem = [System.IO.Path]::GetFileNameWithoutExtension($asmAbs)
$bin = Join-Path $Out 'bin'; $gen = Join-Path $Out 'gen'; $asmDir = Join-Path $Out 'asm'
New-Item -ItemType Directory -Force $bin, $gen, $asmDir | Out-Null
Copy-Item $asmAbs $asmDir -Force

$o = Join-Path $bin "$stem.o"
& "$bu\powerpc-none-elf-as.exe" -a32 -be -mregnames -mpower7 -maltivec -mvsx -mvmx128 -R -o (To-Cyg $o) (To-Cyg $asmAbs)
if ($LASTEXITCODE -ne 0) { throw "as falhou" }
& "$bu\powerpc-none-elf-ld.exe" -A powerpc:common32 -melf32ppc -EB -nostdlib --oformat=binary `
  -Ttext=0x82010000 -e 0x82010000 -o (To-Cyg (Join-Path $bin "$stem.bin")) (To-Cyg $o)
if ($LASTEXITCODE -ne 0) { throw "ld falhou" }
& "$bu\powerpc-none-elf-nm.exe" --numeric-sort (To-Cyg $o) | Out-File -Encoding ascii (Join-Path $bin "$stem.map")
Remove-Item $o
& $Rexglue recompile-tests "--output=$gen" "--bin-dir=$bin" "--asm-dir=$asmDir" *> (Join-Path $Out 'recompile.log')
"rexglue exit=$LASTEXITCODE"
