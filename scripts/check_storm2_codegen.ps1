# Verifica invariantes do C++ gerado do Storm 2 (regressao do codegen).
# Uso: .\scripts\check_storm2_codegen.ps1   (depois de rodar o codegen)
$root = Split-Path -Parent $PSScriptRoot
$gen = Join-Path $root 'storm2\generated\default'
$dump = Join-Path $root 'storm2\image_dump'
$fail = 0

$fatal = (Select-String -Path "$gen\*.cpp" -Pattern 'REX_FATAL\("Unresolved').Count
"desvios nao resolvidos (REX_FATAL): $fatal (esperado 0)"; if ($fatal -ne 0) { $fail++ }

$unimpl = (Select-String -Path "$gen\*.cpp" -Pattern '// UNIMPLEMENTED:').Count
"instrucoes nao implementadas: $unimpl (esperado 0)"; if ($unimpl -ne 0) { $fail++ }

$out = py -3 -I (Join-Path $PSScriptRoot 'find_data_only_functions.py') $dump $gen | Select-Object -First 1
"ponteiros em dados sem funcao: $out"
if ($out -notmatch 'sem codigo gerado: 0$') { $fail++ }

$out2 = py -3 -I (Join-Path $PSScriptRoot 'find_code_const_functions.py') $dump $gen | Select-Object -First 1
"enderecos montados no codigo sem funcao: $out2"
if ($out2 -notmatch 'sem funcao: 0$') { $fail++ }

$out3 = py -3 -I (Join-Path $PSScriptRoot 'audit_switch_tables.py') $gen | Select-Object -Last 1
"tabelas de salto: $out3"
if ($out3 -notmatch 'indice_e_tabela: 0;' -or $out3 -notmatch 'so_funcoes: 0;') { $fail++ }

$trap = (Select-String -Path "$gen\*.cpp" -Pattern 'Switch case out of range').Count
"switch com trap silencioso no default: $trap (esperado 0)"; if ($trap -ne 0) { $fail++ }

if ($fail -eq 0) { "OK" } else { "FALHOU ($fail)"; exit 1 }
