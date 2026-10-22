# Relatório inicial — Marco A

Data: 2026-10-22. Máquina: Windows 11 Pro 10.0.26300 (x64). Neste documento, **[V]** indica fato verificado por comando ou leitura, **[H]** indica hipótese e **[P]** indica item pendente.

## 1. ReXGlue
- [V] Origem: `E:\p3build\rexglue-src`, remote `https://github.com/rexglue/rexglue-sdk.git`, branch `main`, commit `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518` ("Release v0.10.0", 2026-08-21).
- [V] Cópia de trabalho do projeto: `rexglue/`, clonada desse commit, branch **`vita-arm32`**. Os 22 submódulos foram inicializados a partir das cópias locais (sem rede). O original em `E:\p3build` não foi alterado.
- [V] Modificações locais que existiam no original, vindas da sessão do LEGO Batman 2:
  - `src/system/function_dispatcher.cpp`: +7 linhas de **log de depuração** em `InvalidFunctionTrap` (prefixo `[P3]`). Não muda comportamento.
  - `src/kernel/xboxkrnl/xboxkrnl_video.cpp`: só fim de linha (CRLF). O `git diff` não mostra mudança de conteúdo.
  - Submódulos `libmspack`, `moltenvk` e `o1heap` marcados como modificados: são **links simbólicos do git gravados como texto** pelo Windows sem Developer Mode. Isso quebra a compilação do `libmspack` (`lzxd.c:1: error: expected identifier`). Foi corrigido de forma reprodutível com `scripts/fix_symlinks.ps1` (18 arquivos; os links de diretório do MoltenVK só importam no macOS).
- [V] Presets de build: `win-amd64`, `win-arm64`, `linux-amd64`, `linux-arm64`, `mac-amd64`, `mac-arm64`. **Não há alvo ARM32.**
- [V] Opções relevantes: `REXGLUE_BUILD_TESTS` (OFF por padrão), `REXGLUE_ENABLE_TRACY`, `REXGLUE_USE_D3D12`/`VULKAN`.

## 2. Ferramentas
| Ferramenta | Versão | Local |
|---|---|---|
| clang/clang++ | 19.1.7 (x86_64-pc-windows-msvc) | `C:\LLVM\bin` |
| cmake | 3.31.6 | `C:\Users\minoo\p3tools\cmake\bin` |
| ninja | 1.12.1 | `C:\Users\minoo\p3tools` |
| git | 2.49.0.windows.1 | `C:\Users\minoo\p3tools\cmd` |
| arm-vita-eabi-gcc | 15.2.0 | `C:\vitasdk\bin` (VitaSDK build 2026-08-25) |
| vita-elf-create / vita-mksfoex / vita-pack-vpk | toolchain `c527abc` | `C:\vitasdk\bin` |
| binutils PowerPC (Cygwin) | incluído no ReXGlue | `rexglue/tools/binutils` |
| Python | Windows Store launcher | PATH |

- [V] Nada disso estava no PATH. Use `scripts/env.ps1` (dot-source) para configurar.
- [V] vitaGL, vitaShaRK e SDL2 para o Vita **não estão instalados** em `C:\vitasdk\arm-vita-eabi\lib`.
- [V] WSL sem distribuição instalada. Visual Studio não detectado pelo `vswhere`, mas o clang compila contra a STL/SDK do MSVC encontrada pelo CMake.
- [P] Xenia: as pastas `E:\xenia_canary_windows_netplay` e `E:\xenia_edge_windows` existem. O executável e o funcionamento não foram verificados.

## 3. Arquivos do Storm 2
- [V] Pasta: `Downloads\Naruto Storm 2 PT-BR - TokyoGames\Naruto Storm 2 PT-BR - TokyoGames\Naruto Shippuden Storm 2 PTBR\`
- [V] `default.xex`: 9.875.456 bytes, magic `XEX2`, SHA-256 `C0A951350811272CC9DBC69A3C73E8A168065470098CD9301595781D08B1B6AB`.
- [V] `nxeart` (1.343.488 bytes) e `data/` (3,05 GB): 3230 `.xfbin`, 2234 `.lua`, 449 `.nub`, 7 `.wmv`, 2 `.bin`, 2 `.nsh`. Subpastas: `boss cmn effect event ia movie pt rpg skill sound spc stage system ui`.
- [V] **Não há outros `.xex`/`.xexp`/DLLs** na pasta do jogo.
- [V] É uma tradução de fã PT-BR (TokyoGames / JeanKasoki). O LEIA-ME diz textualmente que o projeto "é um port da versão de PC" com "adaptações para o Xbox 360". [H] Os dados podem não ser idênticos aos de um disco retail.
- [P] Title ID, Media ID, versão e lista de imports: a extrair com `rexglue` (Marco D).
- [V] Versão de PC: **não encontrada** (`G:\SteamLibrary` está vazio; nenhuma outra instalação localizada).

## 4. Projeto anterior (LEGO Batman 2) — diagnóstico com evidência
Local: `E:\p3build` (consultado só para leitura).

1. **Port "P3" (XenonRecomp + runtime próprio)**, `logs/run6.log`:
   - Imports sem implementação retornando 0: `MmQueryStatistics`, `KeQueryPerformanceFrequency`, `NtCreateSemaphore`, `NtReleaseSemaphore`, `NtCreateEvent`.
   - Depois: "chamada indireta para 0x82AE90C8 sem implementacao (sem entrada na tabela)" e `EXCEPTION_ACCESS_VIOLATION` (escrita em `base+4`, ou seja, endereço de convidado ~0x4: um ponteiro nulo do convidado).
   - [H] A causa provável é um stub (por exemplo `NtCreateEvent` devolvendo handle 0 / ponteiro não preenchido). **Não é comprovadamente um erro de tradução.**
2. **Projeto ReXGlue `legobatman2`**:
   - `extra_functions.toml` declara à mão funções alcançadas só por ponteiros em `.rdata`, com tamanhos pequenos (4–40 bytes).
   - [V] Para funções declaradas no manifesto (autoridade CONFIG), o ReXGlue **confia no tamanho declarado** (`function_graph.cpp:166`, `phase_discover.cpp:71`).
   - `logs/legocodegen_iter.log` mostra dezenas de `Unresolved conditional branch` / `Unresolved b target` dentro dessas funções, ou seja, o tamanho declarado as cortou no meio.
   - O `function_dispatcher.cpp` foi instrumentado porque havia `Call to invalid or unregistered function`.
   - Os logs de execução `run_rex*.log` estão vazios (0 bytes), então **não há evidência do comportamento em execução** desse build.
   - Conclusão: os defeitos documentados são de **análise de limites de função e configuração manual** (categoria A, análise) e de **runtime/stubs** (categoria B). Não há evidência de erro de semântica de instrução.
   - Lição para o Storm 2: não declarar tamanhos de função à mão sem validar; preferir os tamanhos de `.pdata` e corrigir a descoberta no gerador.

## 5. Arquitetura do código gerado (relevante para ARM32)
- [V] Todo acesso à memória do convidado passa por macros do PCH gerado (`resources/templates/codegen/pch_h.inja` e, nos testes, `templates/test/ppc_config_h.inja`): `REX_LOAD_*`, `REX_STORE_*`, `REX_RAW_ADDR`, `REX_MM_*`. Os loads/stores vetoriais e o `dcbz` também usam `REX_RAW_ADDR` (`builders/memory.cpp`, `builders/system.cpp`).
- [V] Modelo atual: `base + (u32)addr` com `REX_MEMORY_SIZE 0x100000000` (4 GB) e arena em `0x1_0000_0000`/`0x2_0000_0000`. **Exige espaço de endereçamento de 64 bits.**
- [V] Registradores em `u64`, VMX via simde. Há ramos `__aarch64__` em `rex/ppc/intrinsics.h`, mas não `__arm__`.
- [V] O PCH tem ramos para GCC (`__GNUC__`): o compilador do Vita é o GCC 15.

## 6. Testes (win-amd64, Release, clang 19.1.7)
| Etapa | Suíte | Resultado | Log |
|---|---|---|---|
| Linha de base (commit `c94f5eb` limpo) | `ppc_tests` original (166 `.s`) | **1458/1458 casos, 5725 verificações, 3×** | `A3_ppc_tests_baseline_run{1,2,3}.log` |
| Linha de base | `unit_tests` (não compilava; corrigido o link) | 208/213 passam, 1 falha (#6 depfile), 4 pulados pelo upstream | `A5_unit_tests.log` |
| Regressão lote 1 (+6 `.s`) | `ppc_tests` | 4 falhas reais (bugs #1, #2, #3) → corrigidas | `A5_…`, `B1_…`, `B2_…` |
| Regressão lote 2 (+5 `.s`, guiado pelo histograma) | `ppc_tests` | 62 novos, todos passam | `B3_ppc_tests_batch2.log` |
| FPSCR (+1 `.s`) e bench (+1 `.s`, oráculo Python independente) | `ppc_tests` | passam | `C3_…` |
| Estado atual | `ppc_tests` (178 `.s`) | **1585/1585, 6430 verificações** | `C8_ppc.log` |
| Estado atual | `ppc_tests_softmmu` (mesmo código, MMU por software + u64 dividido) | **1585/1585, 3×** | `C2_…`, `C8_ppc_soft.log` |
| Estado atual | `unit_tests` (+2 novos: classificação de alvos, mulh portátil) | novos passam; continua só a falha #6 | `B4_unit_tests.log`, `C6_unit_mulh.log` |
| Harness do Vita compilado no host | runner próprio, MMU por software, página-guarda | **1585/1585** | `C10_host_harness_run.log` |

Cobertura medida contra o Storm 2 (mnemônicos decodificados no C++ gerado dos testes vs. no C++ do jogo): instruções do jogo sem teste caíram de **6,15%** para **0,90%** das ocorrências (`docs/histograma_instrucoes_storm2.md`).

Limitações registradas: o binutils incluído só aceita registradores VMX128 0–7, então as codificações v32–v127 não estão cobertas; não há oráculo de hardware real (os valores esperados vêm da especificação, dos testes Xenia existentes ou do modelo Python).

## 7. Lacunas que impedem compilar para o Vita
1. Sem preset/toolchain ARM32. O runtime (`rexruntime`) depende de D3D12/Vulkan, SDL3, Win32/POSIX mmap.
2. Modelo de memória de 4 GB contíguos, impossível no Vita, que tem ~256 MB de RAM de usuário + 128 MB de CDRAM.
3. Sem backend gráfico para GXM.
4. vitaGL/SDL2 do Vita não instalados.
