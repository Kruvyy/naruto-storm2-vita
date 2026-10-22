# Registro de bugs

Categorias: **A** tradução PowerPC · **B** runtime/compatibilidade · **C** renderização/recursos · **D** áudio/entrada · **E** integração Vita · **F** desempenho · **T** ferramentas/build.

Estados: *confirmado* (reproduzido por execução), *corrigido* (teste falhava antes e passa depois), *hipótese* (sem reprodução).

Commits na branch `vita-arm32` de `rexglue/`.

| # | Cat. | Componente | Resumo | Estado | Teste | Commit | Uso no Storm 2 |
|---|---|---|---|---|---|---|---|
| 1 | A | codegen / despacho | `crclr`, `crset`, `crnot`, `crmove` (aliases do desmontador) sem builder: o código gerado lançava `Unimplemented PPC instruction` em tempo de execução | corrigido | `crlogic.test_crxor_clear`, `test_creqv_set`, `test_crnot`, `test_crmove` | `a2fa2e4` | 0 ocorrências |
| 2 | A | `fctid`/`fctidz` | entrada exatamente 2^63 não saturava (resultado `0x8000…` em vez de `0x7FFF…`): `double(LLONG_MAX)` arredonda para 2^63 e a comparação usava `>` | corrigido | `fpedge.test_fctidz`, `fpedge.test_fctid` | `df45b57` | 740 |
| 3 | A | `fmadds`/`fmsubs`/`fnmadds`/`fnmsubs` | arredondamento duplo `double(float(fma()))`; o PPC arredonda o valor exato uma vez para simples. Erro de 1 ulp em casos de empate | corrigido (arredondar-para-ímpar + ErrFma) | `fpedge.test_f*s_single_rounding` (4) | `df45b57` | 5.577 |
| 4 | T | testes unitários | `unit_tests` não compilava: `hash_test.cpp` inclui `xxhash.h`, mas o xxHash era link privado de `rexcore` | corrigido | build `unit_tests` | `31c84f0` | — |
| 5 | T | ambiente Windows | links simbólicos do git (libmspack/moltenvk/o1heap) gravados como texto sem Developer Mode quebram o build (`lzxd.c:1`) | contornado (`scripts/fix_symlinks.ps1`) | build | — | — |
| 6 | T | testes unitários | `codegen/output_stamp_test.cpp:227-228` ("Depfile escapes characters make treats as syntax") falha no Windows: o escape de espaço/`#` esperado no depfile não aparece | confirmado, não corrigido | `unit_tests` | — | não afeta código gerado |
| 7 | T | testes unitários (upstream) | 4 testes pulados pelo próprio ReXGlue, um deles com a mensagem "BitStream::Write is broken - doesn't byte-swap on store" | registrado | `unit_tests` | — | a avaliar se o runtime usa |
| 8 | A | análise de funções | funções-folha sem entrada `.pdata`, alcançadas só por `b` (tail call), não são descobertas → validação falha ("target not in any function") | contornado no manifesto (3 funções, sem tamanho declarado) | codegen Storm 2 | — | 3 funções (`0x824B6480`, `0x825D0910`, `0x825D1220`) |
| 9 | A | emissão / `classifyTarget` | desvios dentro de blocos fundidos (tail-merge) depois de outra função `.pdata` eram classificados como "desconhecidos" e emitidos como `REX_FATAL` | corrigido | `unit.classifyTarget uses the emitting function…` + `check_storm2_codegen.ps1` | `90ebec9` | 2 desvios (`0x824A4484`, `0x824A4490`) |
| 10 | F/E | codegen | função `0x8280BA00` gera 1,3 MB de C++ (acima de `max_file_size_bytes`) | registrado; risco de memória do compilador no build ARM | — | — | 1 função |
| 11 | A | análise de funções | funções-folha sem `.pdata`, alcançadas **só por ponteiros em dados** (thunks vcall/ajuste, tabelas de callbacks), não eram geradas → chamada pelo ponteiro aborta ("unregistered function"). Mesmo defeito do LB2 | corrigido (opção `scan_data_pointers`, desligada por padrão; ligada no Storm 2) | `find_data_only_functions.py`: 140 → 0; `check_storm2_codegen.ps1` | `7476ec5` | 140 funções; também resolveu o desvio `0x827BC720→6D4` |

## Detalhes

### 1 — aliases de CR
`thirdparty/disasm/ppc-dis.c` decodifica `crxor b,b,b` como `crclr` (`PPC_INST_CRCLR`) e assim por diante. Os operandos repetidos são `PPC_OPERAND_FAKE` e **não** vão para `insn.operands`, então mapear o alias para o builder básico leria operandos inválidos. Por isso cada alias ganhou um builder próprio em `src/codegen/builders/logical.cpp`.

Evidência antes da correção: `scripts/ppc_gen_one.ps1` com o `rexglue.exe` antigo gerou `// UNIMPLEMENTED: crclr/crset/crnot/crmove`. Na execução, `crlogic.test_crxor_clear` e `test_creqv_set` falharam com "Unimplemented PPC instruction" (`docs/logs/A5_ppc_tests_with_regression.log`).

### 2 — `fctid`/`fctidz` em 2^63
Entrada `0x43E0000000000000`: antes `0x8000000000000000`, depois `0x7FFFFFFFFFFFFFFF`. Os demais casos (−2^63, NaN, −2,5) já estavam corretos.

### 3 — FMA de precisão simples
Caso: A=1−2⁻²⁴, C=1+2⁻²³, B=2⁻⁴⁷+2⁻⁶⁰ (todos representáveis em simples). O valor exato é 1+2⁻²⁴+2⁻⁶⁰. O PowerPC dá 1+2⁻²³; o ReXGlue dava 1,0.

Correção: `ppc_fma_to_single()` em `include/rex/ppc/intrinsics.h`. A premissa é que o host arredonde para o mais próximo (verdade nos testes; **a verificar no Vita**).

**Pendência E/F:** o Cortex-A9 (VFPv3) não tem FMA em hardware. `std::fma` no newlib do Vita pode ser lento, ou até não ser fundido. Os testes `fpedge.test_fmadd_fused` e `fmadds_*` precisam rodar no Vita.

### 8 — funções sem `.pdata`
Verificado com `scripts/pdata_query.py` (22.091 entradas na `.pdata`) e `scripts/dis.py`:
- `0x824B6480`: `lwz r3,80(r3); cmplwi; beqlr; b 0x826D6600`, chamada só por `b` a partir de `0x8224B5F8`/`0x8224B610`, que também não têm `.pdata`.
- `0x825D0910` e `0x825D1220`: thunks `r3 = &global; b 0x825DAFC0`, alcançados a partir do seletor em `0x825D9BC0`.

O ReXGlue registra como função nova apenas destinos de `bl` (`phase_discover.cpp:115`), de propósito, porque um `b` para fora pode ser parte da mesma função. A correção no gerador (promover destinos de `b` fora de qualquer região conhecida) fica como melhoria futura, a validar contra o LB2 e o Storm 2.

### 9 — desvios em blocos fundidos
`sub_824A42D8` termina com `b 0x824A4460`, e o código-folha em `0x824A4460` (uma busca em lista ligada, sem `.pdata`) foi fundido nela. Entre as duas existe a função `.pdata` `0x824A43F8`. Para o desvio em `0x824A4484`, `getFunctionContaining()` (função com início mais próximo abaixo) devolvia `0x824A43F8`, que não contém o endereço. Resultado: `TargetKind::Unknown` → `REX_FATAL`. Com a correção, os builders informam a função que estão emitindo, e os dois pontos viraram `goto loc_824A4498` / `goto loc_824A447C`.

### 11 — funções alcançadas só por ponteiros em dados
`scripts/find_data_only_functions.py` lista palavras alinhadas em seções não executáveis que apontam para início de rotina (precedido por `blr`/`bctr`/`b`/padding), fora do interior de qualquer `.pdata`, e que não estão na tabela de funções gerada (`storm2_init.cpp`).

Amostras verificadas por desmontagem:
- `0x821DD460…0x821DD820`: thunks `lwz r12,0(r3); lwz r11,N(r12); mtctr r11; bctr`;
- `0x821DEA78`: folha com 56 referências;
- `0x82211610`: thunk;
- `0x827BC618`: parser `&#x…;` referenciado de `.rdata` `0x821920B4`/`0x82192224`.

Antes da correção, `0x827BC618` não existia no código gerado e uma metade dela (`0x827BC6DC`) virou função separada, com um desvio cruzado para `0x827BC6D4` emitido como `REX_FATAL`.

Depois: 0 candidatos; 2.633 registros feitos pela varredura (a maioria seria achada depois via `bl`); +136 funções no total (30.944 → 31.080 na tabela).

**Risco residual [H]:** um alvo de tabela de saltos de uma função-folha sem `.pdata` poderia ser registrado como função. Mitigação: esses registros são `DISCOVERED` com xrefs e podem ser fundidos pela análise. Verificado no Storm 2: nenhum desvio não resolvido e nenhum `REX_FATAL` restante.
