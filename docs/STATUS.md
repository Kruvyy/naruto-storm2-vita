# Estado dos marcos

Atualizado em 2026-10-22. Legenda: **feito** = verificado por execução; **compilado** = só compilou; **pendente**; **bloqueado** (com o motivo).

| Marco | Estado | Evidência / falta |
|---|---|---|
| A — Auditoria e testes | **feito** | `docs/RELATORIO_INICIAL.md`; linha de base 1458/1458 3×; bugs em `docs/bugs.md` |
| B — Recompilação de testes PPC | **feito no host** | 178 arquivos `.s`, 1585 casos; 5 bugs de tradução/análise corrigidos com teste; 0 instruções não implementadas no Storm 2 |
| C — Cadeia ARM32 | **feito (Vita físico)** | `dist/rexglue_ppc_tests.vpk` (ELF32 ARM EABI5 hard-float, ARMv7/Thumb-2/VFPv3/NEON). Vita3K: 1582/1585 → 3 bugs corrigidos (#12–14) → 1588/1588. **PS Vita físico: 1588/1588, 0 exceções, página-guarda intacta** (`docs/logs/C17_vita_fisico_ppc_tests.log`): acessos desalinhados (`lwz`/`ld`/`stw`), FPSCR (modos de arredondamento), NEON e FMA de software funcionam no Cortex-A9 real |
| D — Inicialização mínima do Storm 2 | **feito no host (x64)**, incluindo batalha medida (695 s sem travar, depois da correção do bug 20) | codegen completo (0 `REX_FATAL`, 0 não implementadas). Build x64 roda 300 s sem travar e sem fechar sozinho. Tabela de imports (`docs/imports.md`: 229 imports, 115 chamados, nenhum STUB chamado). Tela preta corrigida (bug 19): com as opções padrão a imagem aparece do logo da Dolby até o carregamento de "Batalha 1: Naruto vs Sasuke" (`docs/logs/D27_render_fix`; regressão `scripts/check_storm2_render.ps1`). Instruções por quadro e memória medidas **fora de batalha** (abaixo) |
| E — Código e runtime no Vita | **passo 1 feito**: o código do jogo compila para ARM32 (28,8 MiB) | runtime do Vita não começado |
| E–H | **opção B escolhida** (2026-10-23): runtime mínimo e jogo rodando no Vita sem gráficos | relatório: `docs/RELATORIO_VIABILIDADE.md`; plano e projeto da memória: **`docs/PLANO_RUNTIME_VITA.md`** (etapas B1–B5) |

## Cadeia ARM32 — o que foi preciso mudar no ReXGlue
1. `platform.h`: `#error` para 32 bits → `REX_ARCH_ARM32`; Vita → `REX_PLATFORM_VITA`.
2. `fpscr.h`: FPSCR do VFP; tabela host→convidado por plataforma (corrige a troca +∞/−∞ no ARM).
3. Memória: `REX_GUEST_SOFT_MMU` (tabela de páginas de 64 KB) e `REX_GUEST_SPLIT_U64` (o LDRD do ARMv7 falha em endereço desalinhado).
4. `mulhd`/`mulhdu` sem `__int128`.
5. `vsl` sem deslocamento imediato no NEON.
6. Atributos `alias`/`weak` depois de `extern "C"`: o GCC os ignorava e nenhuma função gerada linkava.
7. `roundevenf`/`roundeven` ausentes no newlib (`vita/common/vita_math_compat.c`).

8. FMA exata por software no ARM32 (o newlib usa `vmla.f64`) e conversões float→int sem o caminho portátil do simde.

## Portão 2 — desempenho medido no PS Vita físico
Benchmarks: `bench.*`, código PPC recompilado, MMU por software + u64 dividido, GCC 15.2 `-O3`. Overclock de CPU de 500 MHz informado pelo usuário (não verificado pelo app).

| Ambiente | int (µs/chamada) | fp (µs) | fma (µs) |
|---|---:|---:|---:|
| PC, clang x64, MMU por software | 152,7 | 87,0 | 155,5 |
| Vita3K (não representa o console) | 161,8 | 123,0 | 254,6 |
| **PS Vita físico** | **1174,2** | **713,7** | **1608,1** |

| Laço | Instruções PPC executadas | Vazão no Vita | Ciclos ARM por instrução PPC (a 500 MHz) | Vita vs PC |
|---|---:|---:|---:|---:|
| inteiro/memória (`lwzx add rotlwi xor addi stwx addi andi. bdnz`) | 73.728 | 62,8 M instr/s | ~8,0 | 7,7× mais lento |
| `fmuls`/`fadds` | 16.384 | 23,0 M instr/s | ~21,8 | 8,2× |
| `fmadds` (atalho sem FMA) + `fmuls` | 12.288 | 7,6 M instr/s | ~65 | 10,3× |

Leitura (fatos medidos e hipóteses separados):
- [V] O Vita3K é 6–7× mais rápido que o console nesses laços, então os tempos do emulador não servem para estimar desempenho.
- [V] Código inteiro/memória custa ~8 ciclos ARM por instrução PPC; ponto flutuante simples, ~22; `fmadds`, ~65.
- [H] Ordem de grandeza: um núcleo do Xenon (3,2 GHz) executa centenas de milhões a mais de 1 bilhão de instruções por segundo. O Vita entrega ~23–63 M instr/s de código recompilado por núcleo nestes laços. Se o jogo ocupar uma fração relevante de um núcleo do Xenon por quadro, ele não roda em tempo real sem otimização forte.
- [P] Para decidir de fato é preciso o **custo por quadro do Storm 2** (instruções do convidado executadas por quadro de batalha), medido no build x64 do jogo (Marco D).
- [H] Alavancas ainda não exploradas, a medir:
  - opções do codegen que mantêm registros em variáveis locais (`non_volatile_as_local`, `cr_as_local` etc.; hoje cada registro vive em memória no `PPCContext`);
  - acessos à memória sem `volatile` onde for seguro;
  - mapeamento direto das regiões grandes em vez da tabela de páginas;
  - FMA mais barata;
  - uso dos 3 núcleos do Vita disponíveis para apps.

## Storm 2 no host (x64) — medidas fora de batalha
Execuções de 240 s (`docs/logs/D17_run3`, `D18_ws`). Essas execuções foram feitas antes da correção da tela preta, que afeta só a GPU. Elas cobrem boot, logos, menus e abertura, **não uma batalha**.

| Medida | Valor | Fonte |
|---|---|---|
| Instruções do convidado por quadro | mediana ~26–30 M; p90 ~35–37 M; p99 ~52–70 M | `frame_stats.csv` (contador por bloco gerado pelo codegen; soma conferida com o total de instruções emitidas) |
| Vazão no PC | ~800–870 M instr/s do convidado, ~29,7 quadros/s | idem |
| Divisão por thread | `F8000068` 54,7 %; `F80000B0` 19,9 %; principal 16,9 % (~5 M/quadro) | `thread_insns.csv` |
| Memória do convidado comprometida (pico) | ~480 MiB (virtual ~131 + física ~349) | `GetGuestMemoryStats` por quadro |
| Memória física residente aos 60 s | ~100 MiB | `scripts/guest_working_set.ps1`. A área virtual daquela execução saiu errada: no PowerShell 5.1, `0xA0000000` vira número negativo e a varredura não cobria nada. Corrigido com `0xA0000000L`. A medida certa, sem aliases, é `scripts/guest_resident_unique.ps1` |

Leitura:
- [V] Fora de batalha, o jogo executa ~26–30 M instruções PPC por quadro, a 30 quadros/s.
- [H, **superado**: veja "Custo real em batalha"] No Vita, a ~23–63 M instr/s por núcleo (portão 2), isso dá **cerca de 15–35× menos** do que o necessário, antes de contar a GPU. O portão 2 está desfavorável com os dados atuais. Ainda falta: medir em batalha e testar as alavancas listadas acima.
- [H] Parte das ~222 M chamadas de `NtWaitForSingleObjectEx`/`NtSetEvent`/`NtSignalAndWait` em 240 s pode ser espera ativa entre threads, o que inflaria a contagem. Não verificado.
- [V] O pico comprometido (~480 MiB) excede a RAM de usuário do Vita (~256 MB + 128 MB de CDRAM). O residente medido é bem menor. O portão 1 depende de medir o residente em batalha.

## Custo real em batalha e o que é espera (2026-10-22)
Execução `docs/logs/batalha2`: 695 s, com o usuário jogando batalhas e fechando a janela no fim (saída normal, código 0). Não houve captura de tela; a fase de batalha foi inferida pelo salto de memória e de custo a partir de ~200 s (`scripts/frame_timeline.py`).

| Medida | Valor |
|---|---|
| Instruções contadas por quadro depois do carregamento (provável batalha) | mediana 30–38 M; p90 41–46 M |
| Memória do convidado residente, sem aliases (`scripts/guest_resident_unique.ps1`) | pico **311 MiB** (virtual 103 + física 208); antes da batalha ~241 MiB |
| Memória comprometida (pico) | ~480 MiB (igual a fora de batalha) |

Amostragem das threads (`scripts/profile_guest_thread.ps1`: suspende a thread ~1000×/s e mapeia o RIP para a função do jogo pela tabela de despacho do runtime; logs em `docs/logs/D34_prof`, `D35_prof`):

| Thread | Instruções contadas | CPU no PC (Ryzen 5 4500) | Onde passa o tempo |
|---|---:|---:|---|
| `F8000068` | 495 M/s (52 %) | ~96 % de um núcleo | ~89 % em espera/sinal do Windows; ~8 % na espera ativa `sub_826ADDE0`; ~3 % em outras funções |
| `F80000B0` | 169 M/s (18 %) | ~20 % | 76 % bloqueada; 19 % no código do jogo |
| principal | 127 M/s (13 %) | ~17 % | 84 % bloqueada em `NtSignalAndWait` (`sub_82693390`); 14 % no código do jogo |
| `F8000074`/`78`/`7C` | 26 M/s cada | ~3 % | 95 % bloqueada |

O que `F8000068` faz [V, por desmontagem]: `sub_826A5DC0` compara o contador de fence do dispositivo D3D (`+10908`) com o valor que a GPU escreve de volta (`*(+10896)`). Enquanto a GPU não alcança o fence, ela repete `sub_826ADDE0`, que é espera ativa: `cctpl`, 4×8 `db16cyc` (instruções de atraso, que viram nada no código compilado), `cctpm`, e um limite de 5000 tiques. As ~875 mil chamadas por segundo de cada uma de `NtSignalAndWaitForSingleObjectEx`, `NtWaitForSingleObjectEx` e `NtSetEvent` saem dessa mesma thread (`sub_82693390`, `sub_82564620`, `sub_826934B8`).

Leitura:
- [V] Cerca de metade das instruções contadas é **espera pela GPU emulada**, não trabalho. Os `db16cyc` contam como instrução e não custam nada no código compilado, por isso essa thread "executa" 495 M/s com só ~10 % do tempo no código do jogo.
- [V] O trabalho real está em `F80000B0` (~5,7 M instruções por quadro) e na principal (~4,3 M por quadro), mais ~2,7 M nas três threads de trabalho. São médias da execução inteira, incluindo menus.
- [H] No Vita, a 23–63 M instr/s por núcleo (portão 2), `F80000B0` sozinha limitaria o jogo a **~4–11 quadros/s**, mesmo com cada thread pesada num núcleo próprio. Isso é antes do custo da GPU emulada no Vita e antes da sobrecarga de sincronização do kernel emulado. Antes da investigação, a estimativa era de 15–35× abaixo do necessário; agora é **~3–7× abaixo** para a thread mais pesada.
- [H] O vaivém de espera e sinal custa quase um núcleo inteiro do PC em chamadas ao sistema. No Vita, com as chamadas mais caras, ele roubaria tempo das outras threads. Alavanca: tratar em alto nível a espera do D3D pelo fence (`sub_826A5DC0`), trocando-a por uma espera bloqueante no evento que o processador de comandos sinaliza ao gravar o fence. Isso altera lógica original e só se justifica com a medida de desempenho.

## Onde está o trabalho real em batalha (`docs/logs/batalha3`)
Sessão de 185 s com o usuário jogando. `scripts/profile_session.ps1` capturou a janela e amostrou `F80000B0` e a thread principal por 8 s em cada ciclo. As fases foram conferidas nas capturas (`storm2/captures/batalha3`, fora do git): ciclo 2 título; 3 seleção de personagem; 4 carregamento; **5–7 batalha** (Kakashi vs Neji). Soma dos ciclos com `scripts/aggregate_profiles.py`:

| Thread (batalha, ciclos 5–7) | CPU no PC | Concentração do tempo no código do jogo |
|---|---:|---|
| `F80000B0` | 20–24 % | **2 funções = 80 %** (`sub_82599010` 51,5 %, `sub_8281E500` 28,9 %); 10 funções = 90 %. Igual nos menus (ciclos 1–3: 49 % + 32 %) |
| principal | 20–28 % | **espalhada**: 44 funções = 50 %, 194 = 80 %, 351 = 90 % (de 586 vistas) |

O que cada função faz, pela desmontagem:
- `sub_82599010`: 1014 instruções, um laço principal (`bdnz`), dominado por ponto flutuante simples (`lfs` 129, `stfs` 62, `fmadds` 45, `fmuls` 31).
- `sub_8281E500`: 1030 instruções de inteiros (`rlwinm`, `add`, `srawi`), com 96 `dcbt` (prefetch de cache). Tem cara de decodificador.

[H, evidência forte] `F80000B0` é o **motor de áudio**. Ela executa dezenas de funções em `0x82570000–0x8258E000`, a mesma região de `sub_825702B8`/`sub_825703C8` (`XMACreateContext`/`XMAReleaseContext`) e de `sub_8258A508` (`XAudioSubmitRenderDriverFrame`). O custo é constante em todas as fases e a thread passa ~80 % do tempo bloqueada esperando o próximo bloco. A thread principal quase não toca essa região.

Leitura para o Vita:
- [H] **Áudio:** o trabalho está em 2 funções. Reescrevê-las em código nativo com NEON (mixagem em float, decodificador), ou tratar o áudio em alto nível, pode reduzir várias vezes a thread hoje mais pesada. A troca precisa de teste de equivalência contra a função recompilada (mesma entrada, mesma saída).
- [H] **Lógica do jogo (thread principal):** custo espalhado em centenas de funções, então reescrever à mão não resolve. A ~127 M instr/s (média da execução) e 23–63 M instr/s por núcleo do Vita, ela sozinha limita o jogo a **~5–15 quadros/s**. Chegar a 30 exige 2–5× de ganho geral do código recompilado no ARM32, e isso é o que precisa ser medido agora, com o benchmark e as opções do codegen no Vita físico.
- [H] **Renderização:** a espera pela GPU (`F8000068`) pode virar uma espera bloqueante, liberando um núcleo.
- [P] **GPU no Vita:** sem renderizador GXM, o custo é desconhecido. Renderizar em 960×544 (57 % dos pixels de 1280×720) alivia a GPU, não a CPU.

## Benchmark das opções do codegen (`vita/bench`, `dist/rexglue_bench.vpk`)
O mesmo código PPC é gerado em 5 variantes de modelo de registros: v0 padrão; v1 `ctr/xer/cr` locais; v2 + `r0/r2/r11/r12`; v3 + não voláteis; v4 + `skip_lr/skip_msr`. O programa confere as 5 variantes e mede o tempo de cada uma, intercaladas, ficando com o melhor de 3 rodadas. Há dois laços novos com formato de código de jogo: `struct` (não voláteis, pilha, chamadas, `cr6`) e `vec3` (matriz em `f14–f25`, `fmadds`). Os valores esperados vêm do modelo Python independente.

| Ambiente | Verificação | Ganho da melhor variante sobre v0 |
|---|---|---|
| PC (clang x64, MMU por software) | 30/30 | `struct` 1,20× (v3); `int` 1,03×; `fp`/`fma`/`vec3` ≤ 1,05× |
| Vita3K (não representa o console) | 30/30 | `struct` 1,31× (v3); `int` 1,07×; demais ≤ 1,02× |
| **PS Vita físico** (2026-10-23) | **30/30** (tela verde) | `struct` **1,21×** (v4); `int` 1,05×; `vec3` 1,04×; `fp`/`fma` ~1,00× |

Vita físico, em M instruções PPC/s, v0 → melhor variante (`docs/logs/E1_bench/bench_vita_fisico.log`):

| Laço | v0 | melhor | Comentário |
|---|---:|---:|---|
| `int` (sintético) | 63,2 | 66,5 | igual à medida anterior (62,8) |
| `struct` (formato de lógica de jogo) | 58,9 | **71,5** | o melhor representante da thread principal |
| `fp` (`fmuls`/`fadds`) | 23,3 | 23,3 | |
| `vec3` (matriz com `fmadds`) | 8,3 | 8,6 | **~7× mais lento que inteiros** |
| `fma` | 7,7 | 7,8 | |

**Clock:** o app leu do sistema, no início, **ARM 333 MHz, barramento 222, GPU 41 MHz**. O usuário informou overclock de 500 MHz. Ou o plugin não estava valendo para este app, ou a leitura veio antes de ele agir. Como os tempos batem com o teste anterior (`int` 1166 µs agora, 1174 µs antes), as duas medidas devem ter sido no mesmo clock, qualquer que seja. [P] Rodar de novo com o overclock confirmado para este app. Se os tempos caírem ~1,5×, o clock era 333 MHz.

**No Storm 2 (2026-10-23):** v3 e v2 travam o jogo numa tela estática do início; v1 funciona (bisseção, bug 21). O manifesto passou a usar **v1** (`ctr/xer/cr/reserved` locais). Verificação: imagem normal (`docs/logs/E4_render_v1`), 240 s sem erro a 29,9 quadros/s (`E4_v1_240s`). Na v3 o executável caía de 61,6 para 52,9 MB.

Leitura:
- [V] As opções de registros locais dão ~1,2× no código com formato de jogo e nada no ponto flutuante. No Storm 2 só a v1 é segura por enquanto (1,16× no `struct`); sozinhas, elas não bastam.
- [V] `fmadds` custa ~40 ciclos por instrução neste clock, por causa da FMA exata por software (bug 3 / bug 14). Na lógica do jogo isso pesa pouco: no código estático, `fmadds` é 0,28 % e `fmuls` 0,67 %. No **áudio** pesa muito: `sub_82599010` tem 45 `fmadds` e 31 `fmuls` no laço.
- [H] **Thread principal:** precisa de ~127 M instr/s para 30 quadros/s. A ~68 M/s (v1, `struct`; 71,5 com v4), dá **~16–17 quadros/s** a 333 MHz. Se 500 MHz valerem e a CPU for o gargalo, ~20–25 quadros/s. O barramento e a memória não sobem com o overclock da CPU, então menos de 1,5×.
- [H] **Áudio (`F80000B0`, ~169 M instr/s):** com o laço quente cheio de `fmadds` a ~8 M/s, recompilado ele não cabe nem em vários núcleos do Vita. A reescrita nativa das duas funções quentes (NEON, sem FMA exata, ou com uma FMA mais barata validada contra a original) **é obrigatória**, não opcional.

## Marco E, passo 1: o código do jogo compila para o Vita (2026-10-23)
`vita/storm2/CMakeLists.txt` compila o código gerado (`storm2_vita_manifest.toml`, saída em `storm2/generated_vita`, fora do git) com o VitaSDK (GCC 15.2, ARMv7, `-O2`, MMU por software, acessos de 64 bits divididos, modelo de registros v1) numa biblioteca estática. Ainda não há link: faltam o runtime do Vita, os imports do kernel/XAM, a GPU e o áudio.

| Medida | Valor |
|---|---|
| Arquivos | **112 de 112 compilam sem erro**. O `storm2_register.cpp`, cola do runtime de PC que registra o módulo no KernelState, fica de fora e será substituído |
| Tempo | 18,5 min com 6 processos (Ryzen 5 4500); pico de 1,6 GB num processo do GCC, 3,3 GB somados |
| **Código ARM (text)** | **30 196 393 bytes ≈ 28,8 MiB**; dados 310 KB |
| Expansão | 17,9 bytes de ARM por instrução PPC (1 686 509 instruções) |
| `-Os` (amostra `storm2_recomp.32`) | 10 % menor que `-O2` (233 KB contra 259 KB); velocidade não medida |

Orçamento de memória no Vita [H, a verificar com o runtime real]:

| Parte | Tamanho |
|---|---:|
| Código do jogo | ~28,8 MiB |
| Tabela de despacho indireto (1 ponteiro de 4 bytes por palavra de código, `REX_CODE_SIZE` 0x676294) | ~6,4 MiB |
| Tabela de páginas da MMU por software (65 536 × 4 bytes) | 0,25 MiB |
| Memória do convidado residente, pico medido no PC em batalha | **~311 MiB** (virtual 103 + física 208) |
| **Soma, sem o runtime e sem a GPU** | **~346 MiB** |

O Vita oferece ~256 MB de RAM principal mais 128 MB de CDRAM, esta usada pela GPU. Portanto:
- [V] O código cabe com folga.
- [H] **A memória de dados do jogo é o problema**: o pico residente sozinho passa da RAM principal. Parte da memória "física" do convidado são recursos de GPU (texturas, vértices, alvos de renderização), que no Vita poderiam ir para a CDRAM. Isso precisa ser medido: quanto dos 208 MiB físicos é de GPU.

## Quanto da memória do jogo é de GPU (batalha, `docs/logs/E6_batalha`)
Instrumentação (`--gpu_swap_diag_interval`): o `SharedMemory` marca, por páginas de 4 KiB, cada trecho da memória física do convidado que a GPU emulada pede. As categorias são vértices, índices, textura, destino de resolve e memexport, e a união não conta a mesma página duas vezes. O "acumulado" vale desde o boot; a "janela" vale para o intervalo de 300 trocas (10 s). Textura só entra na janela quando é (re)carregada, então texturas estáticas aparecem no acumulado e não na janela. A batalha (Sasuke vs Neji, ~75–210 s) foi conferida nas capturas.

| Medida | MiB |
|---|---:|
| GPU por janela de 10 s na batalha (união) | **~67**: vértices ~26, índices ~0,7, textura ~40, resolve ~40. Textura e resolve se sobrepõem: alvos de renderização resolvidos e lidos como textura a cada quadro |
| GPU acumulado desde o boot (união) | **136**: textura 108, resolve 43, vértices 30, índices 1,7 |
| Carregamento da batalha | +24 MiB de textura (80,6 → 104,2) |
| Memória física do convidado residente | **192** (comprometida 348) |
| Memória virtual do convidado residente | 92,6 |
| **Total residente único** | **284,6**; outra batalha deu 311 |

Leitura:
- [V] Da memória física residente (192 MiB), entre ~67 MiB (usados pela GPU a cada 10 s) e 136 MiB (já usados alguma vez) são dados de GPU. O resto, de 56 a 125 MiB, mais os 92,6 MiB virtuais, é dado de CPU.
- [H] No Vita, a GPU (GXM) pode ler a RAM principal mapeada, então os dados de GPU do convidado não precisam ir para a CDRAM. O problema é a **segunda cópia**: as texturas do Xenos são ladrilhadas e em formatos próprios, e um renderizador GXM precisa de cópias convertidas (de ~67 a 108 MiB, mais alvos de renderização). No PC é igual: a memória do convidado guarda a textura original e a GPU do host guarda a convertida.
- [H] Conta para o Vita: código 28,8 + despacho 6,4 + convidado 284,6 ≈ **320 MiB, sem runtime e sem as cópias de GPU**, contra 256 MB de RAM principal mais 128 MB de CDRAM. A memória do convidado poderia ser dividida entre RAM principal e CDRAM, mas as cópias convertidas não cabem junto. Caminhos possíveis, todos alterando o comportamento visual ou o carregamento, e só justificáveis com medida:
  1. **texturas em resolução menor** na conversão (metade da largura e da altura = 1/4 da memória);
  2. **liberar ou descomprometer** páginas que o jogo reserva mas não toca (comprometida 348 contra residente 192: a diferença já não usa RAM física no PC, mas no Vita a MMU por software precisa de um modelo de alocação sob demanda equivalente);
  3. **alvos de renderização a 960×544** em vez de 1280×720 (57 % dos pixels).
- **Veredito do portão 1 [H]:** a memória não cabe com a abordagem direta (memória do convidado inteira mais as cópias convertidas). Só cabe com pelo menos uma das três medidas acima, e isso precisa ser demonstrado no Vita.

## Próximo bloqueio concreto (atualizado)
A batalha foi medida (acima). Decisão pendente com o usuário. O portão 2 continua desfavorável para 30 quadros/s. A pergunta agora é se ganhos de 2–3× são possíveis. Experimentos mais baratos que respondem isso:
1. Rodar no Vita físico o benchmark com as opções do codegen que mantêm registros em variáveis locais (`non_volatile_as_local`, `cr_as_local` etc.) e comparar com os 62,8 / 23,0 / 7,6 M instr/s atuais.
2. Medir no Vita uma função real de `F80000B0` (por exemplo `sub_82599010`, a mais quente dela) em vez de laços sintéticos.

Para repetir a medição de batalha: `.\scripts\run_storm2_stats.ps1 -Nome batalhaN -Segundos 0`, junto com `scripts\sample_working_set.ps1` e `scripts\guest_resident_unique.ps1`.

## Bloqueio anterior (resolvido)
Executar o VPK de testes no Vita do usuário (instalar com o VitaShell, abrir "ReXGlue PPC Tests", aguardar a tela verde ou vermelha, apertar qualquer botão e copiar `ux0:data/storm2port/ppc_tests.log`). Isso valida no hardware: acesso desalinhado, FMA de software, modos de arredondamento do VFP, NEON com flush-to-zero e o tempo do benchmark (portão 2).
