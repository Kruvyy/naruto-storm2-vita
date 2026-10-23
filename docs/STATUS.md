# Estado dos marcos

Atualizado em 2026-10-22. Legenda: **feito** = verificado por execução; **compilado** = só compilou; **pendente**; **bloqueado** (com o motivo).

| Marco | Estado | Evidência / falta |
|---|---|---|
| A — Auditoria e testes | **feito** | `docs/RELATORIO_INICIAL.md`; linha de base 1458/1458 3×; bugs em `docs/bugs.md` |
| B — Recompilação de testes PPC | **feito no host** | 178 arquivos `.s`, 1585 casos; 5 bugs de tradução/análise corrigidos com teste; 0 instruções não implementadas no Storm 2 |
| C — Cadeia ARM32 | **feito (Vita físico)** | `dist/rexglue_ppc_tests.vpk` (ELF32 ARM EABI5 hard-float, ARMv7/Thumb-2/VFPv3/NEON). Vita3K: 1582/1585 → 3 bugs corrigidos (#12–14) → 1588/1588. **PS Vita físico: 1588/1588, 0 exceções, página-guarda intacta** (`docs/logs/C17_vita_fisico_ppc_tests.log`): acessos desalinhados (`lwz`/`ld`/`stw`), FPSCR (modos de arredondamento), NEON e FMA de software funcionam no Cortex-A9 real |
| D — Inicialização mínima do Storm 2 | **feito no host (x64)**, incluindo batalha medida (695 s sem travar, depois da correção do bug 20) | codegen completo (0 `REX_FATAL`, 0 não implementadas). Build x64 roda 300 s sem travar e sem fechar sozinho. Tabela de imports (`docs/imports.md`: 229 imports, 115 chamados, nenhum STUB chamado). Tela preta corrigida (bug 19): com as opções padrão a imagem aparece do logo da Dolby até o carregamento de "Batalha 1: Naruto vs Sasuke" (`docs/logs/D27_render_fix`; regressão `scripts/check_storm2_render.ps1`). Instruções por quadro e memória medidas **fora de batalha** (abaixo) |
| E–H | pendente | dependem dos portões 1 (memória) e 2 (desempenho no Vita) |

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

## Próximo bloqueio concreto (atualizado)
A batalha foi medida (acima). Decisão pendente com o usuário. O portão 2 continua desfavorável para 30 quadros/s. A pergunta agora é se ganhos de 2–3× são possíveis. Experimentos mais baratos que respondem isso:
1. Rodar no Vita físico o benchmark com as opções do codegen que mantêm registros em variáveis locais (`non_volatile_as_local`, `cr_as_local` etc.) e comparar com os 62,8 / 23,0 / 7,6 M instr/s atuais.
2. Medir no Vita uma função real de `F80000B0` (por exemplo `sub_82599010`, a mais quente dela) em vez de laços sintéticos.

Para repetir a medição de batalha: `.\scripts\run_storm2_stats.ps1 -Nome batalhaN -Segundos 0`, junto com `scripts\sample_working_set.ps1` e `scripts\guest_resident_unique.ps1`.

## Bloqueio anterior (resolvido)
Executar o VPK de testes no Vita do usuário (instalar com o VitaShell, abrir "ReXGlue PPC Tests", aguardar a tela verde ou vermelha, apertar qualquer botão e copiar `ux0:data/storm2port/ppc_tests.log`). Isso valida no hardware: acesso desalinhado, FMA de software, modos de arredondamento do VFP, NEON com flush-to-zero e o tempo do benchmark (portão 2).
