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

| 12 | A/E | `fctid`/`fctiw` fora do x86 | conversões portáteis do simde usam `round()` (empate para longe do zero, ignora o modo de arredondamento): `fctid(2,5)` dava 3 | corrigido (`ppc_fctid`/`ppc_fctiw`) | `fpedge.test_fctid`, `fpscr.test_fctid_modes`, `unit [fcti]` | `396acac` | 10 (`fctid`) |
| 13 | A/E | `fctidz` fora do x86 | `simde_mm_cvttsd_si64` portátil é um cast sem checar a faixa (indefinido): `fctidz(−∞)` errado no ARM32 | corrigido (`ppc_fctidz`) | `fctixz.test_fctidz_10` | `396acac` | 730 |
| 14 | E | FMA de dupla precisão no Vita | o `fma` do newlib do VitaSDK é `vmla.f64` (verificado por desmontagem de `libm.a`), com dois arredondamentos | corrigido (`ppc_soft_fma`; atalho sem FMA para `fmadds` com entradas simples) | `fpedge.test_f*_fused` (4), `unit [fma]` | `396acac` | 208 em dupla (`fmadd` 120, `fmsub` 66, `fnmsub` 22) |
| 15 | T | Vita3K | `fopen(..., "w")` não truncou o log antigo, deixando linhas obsoletas no fim | contornado (o runner apaga o arquivo antes); não verificado no console | — | — | — |
| 16 | A [H] | `vmaddfp`/`vnmsubfp` | o ReXGlue faz multiplicação e soma separadas (dois arredondamentos) em todas as plataformas; não se sabe se o VMX128 do Xenon funde a operação | hipótese, sem oráculo de hardware | — | — | 980 + 45 |
| 17 | A | análise de funções | funções sem `.pdata` cujo endereço só aparece no código como constante `lis`+`addi`/`ori` (ponteiro de callback passado em registrador) não eram geradas → `FATAL Call to invalid or unregistered function at guest address 0x8224B8C0` na 1ª execução do Storm 2 | corrigido (opção `scan_code_address_constants`, desligada por padrão; ligada no Storm 2) | `find_code_const_functions.py`: candidatos → 0; `check_storm2_codegen.ps1`; execução de 240 s sem FATAL (`docs/logs/D17_run3`) | `3023ce9` | 693 registros, +6 funções novas |
| 18 | B | `NtQueryInformationFile` | a classe `XFileXctdCompressionInformation` não é implementada pelo runtime: devolve `X_STATUS_INVALID_PARAMETER` e registra erro | confirmado; o jogo segue em frente (provavelmente trata como arquivo não comprimido) [H] | — | — | ~260 chamadas em 240 s |
| 19 | C | render targets D3D12 (caminho RTV) | **tela preta**: o padrão de `execute_unclipped_draw_vs_on_cpu` foi trocado de `true` (Xenia) para `false` no port do ReXGlue. Desenhos sem recorte (quads em espaço de tela, limpezas) passavam a "ocupar" a EDRAM inteira, e as transferências de posse entre render targets zeravam o conteúdo. O frontbuffer apresentado era 0 em todos os canais | corrigido (padrão `true` restaurado, com a descrição original) | `scripts/check_storm2_render.ps1` (falhava: 0 % aceso; passa: 100 %) | `d31d6c4` | todas as telas |
| 20 | A | tabelas de salto (`bctr`) | tabelas de **ponteiros de função** (`table[i]()` como chamada de cauda, sem verificação de limite) eram emitidas como `switch` com só as entradas recuperadas (1–2) e `default: __builtin_trap()`. O detector ainda tomava o registro da tabela como índice (`lwzx rD,rIdx,rTab` com operandos invertidos). Na batalha: `STATUS_ILLEGAL_INSTRUCTION` (`ud2` em `sub_822185C0`) | corrigido: tabela cujos alvos são todos outras funções vira despacho indireto pelo `ctr`; o `default` de todo `switch` passa a despachar pelo `ctr` em vez de trap mudo | `unit [codegen][dispatch]` (4 casos); `check_storm2_codegen.ps1` (auditoria: 9 → 0 tabelas de despacho emitidas como `switch`, 138 → 0 traps no `default`) | `933a33b` | 9 funções; 129 `switch` internos |
| 21 | A | modelo de registros do codegen | `non_argument_as_local` (r0/r2/r11/r12 locais) e `non_volatile_as_local` (r14–r31 locais) fazem o Storm 2 travar numa tela estática do início (27 desenhos e 1 resolve por quadro, sem progredir); só `ctr/xer/cr/reserved` locais (v1) funciona | contornado: o Storm 2 usa v1 (1,16× no laço `struct` do Vita, contra 1,21× da v3). Causa da v2 provável: 14 chamadas a `__savevmx`/`__restvmx`, que recebem o endereço em `r12` (`scripts/find_nonabi_inputs.py`). Causa da v3 não identificada [H: código compartilhado entre funções com r14–r31 vivos] | bisseção v3/v2/v1 com `check_storm2_render.ps1` (`docs/logs/E2_*`, `E3_*`, `E4_*`) | — | todas as telas |
| 22 | B | `ppc/stack.h`, `system/xfile.h` | escreviam na memória do convidado com `base + endereço`, sem o deslocamento de 4 KiB que o Windows exige na visão `0xE0000000` (e sem tradução nenhuma com a MMU por software) | corrigido por leitura de código, não observado em execução: agora usam `GuestPtr`. No Storm 2 a pilha fica em `0x7xxxxxxx`; um buffer de diretório em `0xE0000000` cairia 4 KiB fora | `soft_mmu.Guest code base is the page table` (`GuestPtr` = tabela) | `84bb864` | não observado |
| 24 | B | `core/utf8.cpp` (`hash_fnv1a`) | as constantes de 64 bits do FNV-1a eram guardadas em `size_t`: num host de 32 bits viravam outros números (avisos `-Woverflow` no GCC do Vita) | corrigido: calcula em 64 bits e reduz no fim; no PC o resultado é o mesmo | suíte do PC (3410/3411) + compilação do Vita sem aviso | `fd46f2c` | todas as buscas de nome por hash |
| 25 | B | `kernel/crt/file.cpp`, `xboxkrnl_io_info.cpp` | `size_t >> 32` (tamanho de arquivo, hash) é comportamento indefinido com `size_t` de 32 bits | corrigido: converte para 64 bits antes | compilação do Vita sem aviso | `fd46f2c` | `FindFirstFile`/`XFileSectorInformation` |
| 23 | T | build do ReXGlue | duas árvores de build escreviam os binários na mesma pasta `out/<plataforma>`: a árvore da MMU por software sobrescreveria o `rexruntime.dll` do build normal | corrigido antes de acontecer: a árvore com `REXGLUE_SOFT_MMU` usa `out/<plataforma>-softmmu` | conferido no configure (`Output Dir`) | `84bb864` | — |
| 26 | D/E | áudio XMA (`audio/xma_context.cpp`, `xma_decoder.cpp`) | os 320 contextos XMA criavam o decodificador do FFmpeg na partida (~0,6 MiB cada, quase tudo estado do WMA Pro: ~190 MiB do host). No Vita o heap de 48 MiB acabou no contexto 58 ("Couldn't allocate context" 262 vezes) e o app abortou (`abort` → `_kill_r`, Vita3K) | corrigido: o decodificador é criado no primeiro `XMACreateContext` daquele contexto e fica até o fim; sem memória, o `XMACreateContext` falha com `X_STATUS_NO_MEMORY` em vez de derrubar o app | Vita3K: antes aborta em 1 s; depois roda (`docs/logs/F3_storm2_vita3k`); PC: execução de 60 s (`F4`) | `039d72e` | 20 `XMACreateContext` em ~6 min, menos de 8 contextos com decodificador (o log avisa no 1º e a cada 8) |
| 27 | B/F | `system/xthread.cpp` (`SetActiveCpu`) | (a) o aviso "Too few processor cores" saía a cada troca de afinidade: 16.697 linhas em ~2 min no Vita3K, cada uma gravada no cartão de memória; (b) no Vita a afinidade nunca era aplicada, porque a condição pede 6 processadores lógicos | corrigido: aviso uma vez; no Vita aplica o mapeamento da B3 (thread de hardware i → núcleo i/2), desligável por `ignore_thread_affinities`; máscara repetida não chama o sistema de novo | Vita3K: 1 aviso no log; efeito no desempenho [P console] | `039d72e` | todas as threads |
| 28 | T | link do app do Vita | o kernel referenciava os diálogos ImGui do XAM, as cvars de modo de vídeo (definidas em `ui/window.cpp`) e `WindowedAppContext`; o FFmpeg usava `posix_memalign`, ausente no newlib | corrigido: no Vita os diálogos ficam fora e todo aviso do XAM segue o caminho já usado quando não há ImGui; cvars de vídeo em `ui/display_cvars.cpp`; `HAVE_POSIX_MEMALIGN 0` na configuração do FFmpeg do Vita (`memalign`) | link do `storm2_vita` | `039d72e` | — |
| 29 | E | `core/memory_vita.cpp` | os blocos vinham só da RAM principal (`USER_RW`): ao acabar, o jogo pararia com 26 MiB de RAM contígua e 112 MiB de CDRAM livres | corrigido: depois da RAM principal tenta a RAM contígua e a CDRAM (cacheada, depois sem cache), com aviso no primeiro bloco de cada | não verificável no Vita3K, que não limita a memória [P console] | `039d72e` | blocos do convidado além da RAM principal |
| 30 | T | `vita-elf-create` (VitaSDK de 2026-08-25) | quando o segmento de código termina a menos de ~3,6 KiB de um limite de 64 KiB, a conversão falha ("Cannot allocate N bytes for SCE data at end of segment", visto com um depurador mínimo) e o erro não tratado vira `0xC0000374` (heap corrompido). Apareceu ao crescer o código em 3 KB | contornado: `-Wl,--defsym,__sce_headroom=0x4000` (opção do `armvita.x`) em `vita/storm2` | build do VPK | port | — |
| 31 | C/E | `gles/gl_api.cpp` | o retorno de `vglInitWithCustomSizes` era tratado como falha; ele é `GL_TRUE` só quando a resolução foi reduzida | corrigido: checa `is_shark_online` | Vita3K | `012ef24` | partida |
| 32 | C/E | `gles_gpu.cpp` (FBO) | framebuffer só de profundidade é incompleto no vitaGL (0x8CD7) | corrigido: anexo de cor descartável por tamanho | log do Vita3K | `012ef24` | passes de sombra/profundidade |
| 33 | C/F/E | `gles_gpu.cpp` (resolves) | `glCopyTexSubImage2D` no vitaGL = `glReadPixels` com espera da GPU + novo upload, ~30 por quadro; no Vita3K a leitura volta zerada (tela preta, 0,2 quadros/s) | corrigido: resolve como desenho num FBO | PC idêntico byte a byte; Vita3K até 15 quadros/s | `012ef24` | todos os quadros |
| 34 | C/E | `gles_gpu.cpp` (uniforms) | o vitaGL devolve como posição o endereço negado do registro do uniform, que pode ser negativo; o código tratava `< 0` como ausente | corrigido: só −1 é ausente | — | `012ef24` | todos os programas |
| 35 | C/E | `gles_gpu.cpp` (tesoura, limpezas) | com a tesoura do GL ligada, o vitaGL descarta desenhos e limpezas em FBOs | corrigido: viewport ∩ tesoura com compensação na NDC; limpezas como desenho | `gles_present_test=6`; `gles_debug_solid_ps` | `012ef24` | todos os desenhos |
| 36 | C/E | `glsl_translator.cpp` (kill) | `discard` dentro de `xe_body()` em `if` aninhados matava todos os pixels dos shaders do jogo no vitaGL/`libshacccg` | corrigido: `xe_kill` + `discard` só no `main()` (causa exata no compilador [H]; o teste isolado não reproduz) | Vita3K: tela "AGUARDE" | `012ef24` | shaders com kill |
| 37 | C/E | vitaGL (construções GLSL) | array de uniform com índice calculado lê zeros; `for` com `continue`/`break` dá resultado errado | confirmado (`gles_present_test=5`, células 4 e 7), não corrigido | Vita3K | — | endereçamento relativo, shaders com saltos (1 de 66 no censo) |
| 38 | E/F | memória da GPU no vitaGL | ~100 s depois da partida o Vita3K caía dentro do `libshacccg` com o heap do newlib cheio | corrigido em grande parte: relatório de memória, despejo de texturas sem uso, alvos de 736 linhas, FBOs de resolve por tamanho, passes de profundidade em alvo 16×16, pools maiores; o jogo passa desse ponto (uso ~76–99 MiB) | `docs/logs/G1f_vita3k` | `fefc455` | abertura em 3D |
| 39 | C/E | vitaGL (culling) | GL_FRONT/GL_FRONT_AND_BACK ligam o modo "sem polígonos": desenhos seguintes descartados antes da cena, splash do vitaGL nunca termina, trocas de quadro ignoradas (logo piscando) | corrigido: só GL_BACK (frente = trás com ordem invertida; as duas faces = pular) e fim do splash na inicialização | Vita3K, gravação do usuário | `fefc455` | todos os quadros |
| 40 | C/E | vitaGL (FBO reaproveitado) | trocar a textura de um FBO com cena aberta estraga o trabalho da GPU (tela congelada) | corrigido: dois FBOs por tamanho de resolve, alternados | `gles_present_test=7` | `fefc455` | resolves seguidos do mesmo tamanho |
| 41 | C/E/F | vitaGL (uniforms) | `vglSetUniformData` escreve sem limite de tamanho de array ou de componentes, em buffers alocados no heap do newlib (corrupção de heap) | corrigido: tamanho mantido pelo programa (`glGetActiveUniform`) para arrays e vec4; shaders auxiliares com vec2/float | PC igual; Vita3K | `fefc455` | todos os programas |
| 42 | E | `core/threading_vita.cpp` | cada XThread pedia 16 MiB de pilha do host | corrigido: limite `vita_thread_stack_max_kib` (2 MiB) | — | `fefc455` | ~23 threads |
| 43 | T | Vita3K (configuração do usuário) | com `disable-surface-sync: true` a imagem do jogo fica branca; com `resolution-multiplier: 0.5` a saída é 480×272. Confundiu uma bisseção inteira | registrado: testes com surface sync ligado e 1× | — | — | — |
| 44 | C/E | GLES no vitaGL (tentativas) | um FBO por alvo de cor deixa a tela preta; pular passes só de profundidade faz a tela alternar com a imagem do splash | revertido/desligado (cvar `gles_skip_depth_only`); causa não encontrada [H] | Vita3K | `fefc455` | — |
| 45 | B/E | camada de threads no Vita | no início da abertura (~quadro 520–565, só com o renderizador): leitura inválida em `pte_osTlsGetValue` e depois `std::thread` começando de estado destruído (PC=0) | confirmado no emulador, não corrigido | `docs/logs/G1f_vita3k/vita3k_crash.log` | — | abertura |

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

### 12–14 — primeira execução em ARMv7 (Vita3K)
O usuário rodou o VPK no **Vita3K** (emulador; log em `docs/logs/C11_vita3k_ppc_tests.log`): 1582/1585, com 3 falhas: `fctixz.test_fctidz_10`, `fpedge.test_fmadd_fused` e `fpedge.test_fctid`. Nenhuma travou o app, e a página-guarda ficou intacta.
- Causas confirmadas: no código-fonte do simde (`sse2.h`, ramos não-x86 de `cvtsd_si*`/`cvttsd_si64`) e na desmontagem de `lib_a-s_fma.o` do newlib (`vmla.f64 d2, d0, d1`).
- Depois da correção: **1588/1588 no Vita3K** (`docs/logs/C16_vita3k_ppc_tests.log`).
- Durante a correção, o teste de casos-limite pegou um bug no meu primeiro rascunho da FMA de software: produto finito que estoura para ∞ somado a z = −∞ dava NaN em vez de −∞. Foi corrigido antes do commit.
- Os itens 12 e 13 também afetariam ARM64, já que os ramos portáteis do simde são os mesmos.

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

### 17 — funções alcançadas só por constantes de endereço no código
Na 1ª execução (`docs/logs/D14_run1/storm2.log`) o jogo abortou em `0x8224B8C0`, que não tem `.pdata`, não é alvo de `bl` e não aparece em seções de dados. O endereço é montado no código em 24 pontos e passado como argumento (r7/r8) de uma chamada, ou seja, é um callback. Exemplo verificado por desmontagem: `0x822365DC lis r9,0x8225` / `0x822365E4 addi r8,r9,-0x4740`, seguido de `bl`.

A opção `[analysis] scan_code_address_constants` (`src/codegen/phase_register.cpp`) varre as seções executáveis atrás de `lis rD,hi` seguido, em até 8 instruções, de `addi`/`ori` sobre rD. O alvo precisa ser alinhado, executável, fora do interior de uma `.pdata`, não ser import nem função conhecida, e vir depois de um terminador (`blr`/`bctr`/`b`/padding). Pares seguidos de `mtctr` em até 5 instruções são ignorados, porque são bases de tabelas de `switch`. Os alvos são registrados como `DISCOVERED` (podem ser fundidos).

Resultado: 693 registros, 6 funções novas, `scripts/find_code_const_functions.py` sem candidatos restantes, e a execução de 240 s chegou ao fim sem FATAL.

**Risco residual [H]:** um par `lis`/`addi` que monta um endereço de dado dentro de `.text` (por exemplo, uma tabela) poderia virar função falsa. Mitigação: exige terminador antes do alvo, ignora bases de `switch` (`mtctr`) e registra como `DISCOVERED`. Verificado no Storm 2: o codegen continua sem `REX_FATAL` e sem desvios não resolvidos.

### 19 — tela preta no build x64 (D3D12, caminho RTV)
Sintoma: janela 100 % preta durante 300 s, com o jogo rodando. Não era fechamento: o processo só terminava quando o script o encerrava.

Como foi isolado:
1. Captura da janela por `PrintWindow` (`scripts/capture_storm2.ps1`): brilho médio 0,00 e 0,00 % de pixels não pretos em todas as amostras (`D20_cap`).
2. Referência: o Xenia Canary (`E:\xenia_canary_windows_netplay`, mesma cópia do jogo, mesma GPU AMD, mesma escolha automática de caminho RTV) mostrou a interface e a abertura em tempo real (`D21_xenia_ref`). Isso também provou que o método de captura enxerga D3D12.
3. Diagnóstico novo `--gpu_swap_diag_interval=N` (`src/graphics/d3d12/command_processor.cpp`, desligado por padrão). A cada N trocas ele registra desenhos, resolves e destinos, e lê de volta a textura apresentada. Resultado (`D23_swapdiag`): ~150 desenhos e ~31 resolves por quadro; um resolve grava no frontbuffer `0B000000`; a tabela de gama foi gravada pelo jogo (vermelho[128] = 558); **e o conteúdo da textura é 0 nos 4 canais**. Ou seja, a apresentação funcionava e apresentava um quadro vazio.
4. Amostras de desenhos (`D24_drawdiag`): constantes de shader e vértices plausíveis, sem NaN nem lixo. Nada indicou erro de tradução da CPU.
5. Testes A/B (`scripts/gpu_ab_storm2.ps1`, `D25_*`, `D26_*`):

   | Variante | Resultado |
   |---|---|
   | `async_shader_compilation=false`, `direct_host_resolve=false`, `d3d12_readback_resolve=true`, `gamma_render_target_as_unorm16=false`, `mrt_edram_used_range_clamp_to_min=false` | continuou preto |
   | `render_target_path_d3d12=rov` | só o texto "AGUARDE 100%" |
   | `execute_unclipped_draw_vs_on_cpu=true` | imagem completa |

Causa: em `src/graphics/util/draw_extent_estimator.cpp` o ReXGlue comentou o `DEFINE_bool(execute_unclipped_draw_vs_on_cpu, true, …)` do Xenia e declarou `REXCVAR_DEFINE_BOOL(…, false, …)`. Sem essa estimativa, um desenho sem recorte é tratado como se usasse até a EDRAM inteira, e as transferências de posse de faixas da EDRAM entre render targets do host apagavam os alvos que o jogo depois resolve.

Correção: padrão `true` restaurado, com a descrição do Xenia. Depois dela, `scripts/check_storm2_render.ps1` sem opções extras: frontbuffer 100 % aceso, janela 96,7–99,4 % não preta, do logo da Dolby até "Batalha 1: Naruto vs Sasuke, AGUARDE 87%" (`D27_render_fix`). Suíte `ctest`: 3394/3395 (a falha é o item 6, anterior).

**Auditoria de padrões** (`scripts/audit_cvar_defaults.py`): só 15 definições originais do Xenia ficaram comentadas no código. Depois da correção, a única divergência restante é `primitive_processor_cache_min_indices` (Xenia 4096, ReXGlue 0), que afeta só o cache de primitivas processadas, ou seja, custo de CPU, não a imagem. Ficou sem mudança, por falta de evidência de problema. **Limitação:** as demais cvars foram convertidas sem deixar o original. Uma auditoria completa exige o código-fonte do Xenia Canary, que não está nesta máquina.

### 20 — tabelas de ponteiros de função emitidas como `switch` (travamento na batalha)
Sintoma: na 1ª tentativa de batalha (`docs/logs/batalha1`), o processo morreu aos ~75 s com `0xC000001D` (`STATUS_ILLEGAL_INSTRUCTION`), sem mensagem no log. Log de eventos do Windows: falha em `storm2.exe+0x1D9D948`. A desmontagem (`llvm-objdump`) mostrou um `ud2`, que é como o Clang emite `__builtin_trap()` no Windows.

Função: `sub_822185C0` (`lis r11,0x8201; slwi r10,r3,2; addi r11,r11,-3568; lwzx r11,r10,r11; mtctr r11; bctr`). Ela lê um ponteiro de função de uma tabela em `.rdata` (`0x8200F210`: `0x822185D8`, `0x82218620`×N, `0x82218668`…) e salta para ele. Código emitido antes:
```
switch (ctx.r11.u32) { case 0: sub_822185D8(ctx, base); return;
                       default: __builtin_trap(); }
```

Duas falhas no `detectJumpTable` (`src/codegen/function_scanner.cpp`), confirmadas no rastro `docs/logs/D3_codegen_trace*.log`:
1. Ele toma `RB` do `lwzx` como índice. Aqui os operandos estão invertidos, então o `switch` ficou sobre `r11`, que é o endereço lido, não o índice.
2. Sem verificação de limite, ele lê entradas até a primeira fora da "região" da função. A entrada [1] (`0x82218620`) já estava fora, então sobrou 1 alvo. Esse alvo é início de outra função e foi tirado da análise de blocos, mas a tabela continuou sendo usada na emissão.

Auditoria do código gerado (`scripts/audit_switch_tables.py`): 9 das 138 tabelas tinham exatamente esse padrão (operandos invertidos e todos os alvos em outras funções); nenhuma tabela interna tinha operandos invertidos.

Correção (`rexglue`):
- `isFunctionDispatchTable()`: uma tabela detectada automaticamente cujos alvos são todos inícios de outras funções não é registrada. O `bctr` vira `REX_CALL_INDIRECT_FUNC(ctr)`, o tratamento que o ReXGlue já dava a um `bctr` sem tabela.
- No `default` de todo `switch`, `__builtin_trap()` virou despacho pelo `ctr`. Se o alvo é uma função registrada, ela roda; se não é, o runtime para com o endereço do convidado, em vez de um `ud2` sem diagnóstico.

Depois: 129 `switch` (internos), 0 tabelas de despacho como `switch`, 0 traps no `default`. `check_storm2_codegen.ps1` OK; `ctest` 3404/3405 (a falha é o item 6).

**Lacunas:**
- O detector ainda não trata `lwzx` com operandos invertidos em tabelas **internas**. Hoje não há nenhum caso desses no Storm 2, e a auditoria acusa se aparecer.
- A suíte PPC não cobre despacho indireto: no harness, `REX_CALL_INDIRECT_FUNC` cai no resolvedor global, que não existe ali. Um teste `.s` desse padrão exige dar ao harness uma tabela de funções.
