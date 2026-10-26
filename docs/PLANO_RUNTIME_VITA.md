# Plano — runtime mínimo do Vita e Storm 2 rodando sem gráficos (opção B)

Data: 2026-10-23. Objetivo (de `docs/RELATORIO_VIABILIDADE.md`, opção B): rodar o código recompilado do Storm 2 no PS Vita físico, **sem renderizar**, para medir **quadros/s reais de CPU** e **memória real**. Legenda: [V] verificado; [H] hipótese; [P] pendente.

## Fatos que definem o projeto

1. **Aproveitar o runtime do ReXGlue.** [V] O runtime sem GPU e sem interface (`core`, `system`, `kernel`, `filesystem`, `audio`, `input`) tem ~1,7 MB de fonte, e o código dependente de sistema operacional já está separado em `*_posix.cpp` / `*_win.cpp` (threads, fibers, relógio, memória, arquivos, exceções). Escrever um kernel novo do zero seria mais trabalho e menos fiel. Esse runtime roda o Storm 2 no PC: 115 imports usados, nenhum stub alcançado.
2. **Sem GPU, o jogo trava antes do primeiro quadro.** [V] Sem plugin de GPU, as funções `Vd*` são ignoradas, a fila de comandos nunca anda e o `VdSwap` nunca é chamado (`docs/logs/E8_headless`). **É preciso uma GPU nula**, que leia a fila PM4, execute escritas de registro, fences, escritas em memória e interrupções, e conte os swaps, sem desenhar.
3. **A memória do jogo está em 3 blocos grandes, preenchidos a partir do começo.** [V] (`docs/logs/E7_allocs2`, `scripts/alloc_residency.ps1`, ~75 s de execução):

   | Bloco | Reservado | Tocado | Padrão |
   |---|---:|---:|---|
   | físico `0xAB000000` | 272 MiB | 162 MiB | prefixo: as faixas de 8 MiB de 0 a 21 estão cheias, as de 22 a 33 vazias |
   | virtual `0x40F40000` | 70 MiB | 31 MiB | prefixo (~30 MiB) mais um pedaço no fim |
   | físico `0xDC000000` (páginas de 16 MB) | 48 MiB | 22 MiB | espalhado |
   | 235 blocos menores | ~43 MiB | ~37 MiB | quase todos tocados |

   O Vita não tem paginação sob demanda para apps: um bloco ocupa RAM física ao ser alocado. Não dá para entregar os ~435 MiB reservados, mas dá para entregar **só o prefixo usado** dos dois blocos grandes.

## Projeto da memória (MMU por software, páginas de 4 KiB)
Implementado no ReXGlue como `REXGLUE_SOFT_MMU` (opção de CMake; preset `win-amd64-softmmu`). Desenho final, que substitui o de 64 KiB:
- **Tabela de 4 KiB** (`base[endereço >> 12]`, 1M entradas). Páginas de 64 KiB não servem: a visão `0xE0000000` enxerga a memória física deslocada de `0x1000` (`E0000000 + x` → físico `x + 0x1000`), o que uma entrada de 64 KiB não representa. O template do gerador agora usa 12 por padrão; os harnesses `vita/ppc_tests` e `vita/bench` definem `REX_GUEST_PAGE_SHIFT=16`, porque montam a própria tabela. Custo da tabela: 4 MiB no Vita (8 MiB no PC), mais 0,5 MiB da tabela física.
- **Cada região reservada ganha um bloco contíguo do host**, nos quatro heaps virtuais e no heap físico. Todo acesso que fica dentro de uma alocação do convidado fica dentro de um bloco, e os `TranslateVirtual` do kernel não mudam. Regiões reservadas sem acesso só ganham memória quando são usadas (comprometidas ou liberadas para leitura).
- **Visões da memória física** (`0xA0000000`, `0xC0000000`, `0xE0000000` e `0x7F000000`, que são os primeiros 16 MiB) apontam para os blocos físicos. `0x9xxxxxxx` repete `0x8xxxxxxx`, como no arranjo plano.
- **Proteção:** páginas sem acesso apontam para uma página que falha no PC. Somente leitura não é imposto.
- **Páginas-guarda:**
  - memória liberada e memória física sem alocação apontam para uma página de zeros compartilhada; uma escrita ali é registrada no log;
  - cada região com limite (`--soft_mmu_caps=0xENDEREÇO=MiB,...`) tem a própria guarda, e uma escrita além do limite **para o programa com mensagem** (região e limite), sem corrupção silenciosa.
  - As guardas são conferidas a cada `VdSwap`.
- **Tabela de funções:** um vetor do host com um ponteiro por palavra de código, publicado ao código gerado por `rex_guest_func_table`. O runtime grava o endereço por `PPCImageInfo::guest_func_table_slot` e recusa a execução se código e runtime usarem modelos de memória diferentes. Limitação: um módulo recompilado por processo. Custo: (código + reserva de thunks) bytes no Vita, ~6,5 MiB no Storm 2.
- **MMIO:** o acesso reconhecido pelo gerador passa por `REX_MM_*`. O resto cai numa janela sem acesso, que o tratador de falhas do PC completa e registra no log ("would crash" no Vita).
- **Fora do modo por software:** D3D12/Vulkan (precisam da memória física num intervalo só do host e de proteção de escrita), savestates (`Memory::Save/Restore`) e testes do ReXGlue.
- [H] Orçamento: ~43 MiB de blocos pequenos + prefixo do físico grande (~180 MiB em batalha, a medir) + ~32 MiB do virtual + 48 MiB do físico de 16 MB ≈ **300 MiB**, mais 35 MiB de código e tabelas. Passa dos ~256 MB padrão do Vita. [P] Verificar no console quanta RAM o app consegue com o modo de memória estendida (`ATTRIBUTE2` no `param.sfo`, usado por ports conhecidos), medindo com `sceKernelGetFreeMemorySize`. Se não bastar, a medida de CPU continua possível com um limite menor, até a página-guarda acusar.

## Etapas
Cada etapa só termina com a verificação indicada. Nada é dado como feito sem execução.

| # | Etapa | Verificação |
|---|---|---|
| B1 | **GPU nula** no ReXGlue: subclasse do `CommandProcessor` que reaproveita a leitura de PM4 da classe base, ignora desenhos e resolves, e executa registros, `EVENT_WRITE`/`MEM_WRITE`/fences, interrupções e `XE_SWAP` | no PC, Storm 2 sem renderizar chega às batalhas, com o jogador jogando às cegas ou com entrada reproduzida: swaps a ~30/s, `import_stats` igual à execução com D3D12 |
| B2 | **Memória por software no runtime de PC**: a classe `Memory` com tabela de 64 KiB, alocações contíguas por bloco, prefixo + página-guarda nos blocos grandes | no PC: Storm 2 com `REX_GUEST_SOFT_MMU` (código gerado e runtime) e GPU nula roda igual a B1; `check_storm2_render.ps1` com D3D12 também passa |
| B3 | **Camada de plataforma do Vita** (`*_vita.cpp`): threads (pthread do VitaSDK ou `sceKernel*Thread`), fibers (`SceFiber`), relógio, arquivos (`sceIo`), blocos de memória; sem tratador de exceção (o MMIO já é tratado no código gerado), sem bibliotecas dinâmicas, sem sockets, sem SDL | `core`/`system`/`kernel`/`filesystem` compilam com o VitaSDK; testes unitários portáveis rodando no Vita |
| B4 | **App `storm2` do Vita**: liga runtime, GPU nula e o código gerado (`vita/storm2`); dados do jogo em `ux0:data/storm2port/game`; entrada por `sceCtrl`; áudio sem som, mas com o XMA decodificado e o relógio de áudio em tempo real (sem isso as threads de áudio do jogo podem travar); log de quadros/s, memória livre e CPU por thread | o VPK abre, chega ao menu e à batalha, e grava o log de medidas |
| B5 | **Medida no console** | log do usuário: quadros/s de CPU em batalha, memória usada, threads |

### Andamento
- **B1 — feito no PC (2026-10-23).**
  - `rex::graphics::null` (`include/rex/graphics/null/null_gpu.h`) e `--gpu_backend=null` (`--gpu_backend` é novo no `ReXApp`). Sem renderizar, o Storm 2 roda a 29,9 quadros/s, com ~121 desenhos e 31 resolves por quadro, os mesmos números do D3D12, e 25,4 M instruções por quadro (mediana; 30,8 M com D3D12, por causa de menos espera ativa pela GPU). Log: `docs/logs/E10_nullgpu_120s`.
  - **Falta** chegar a uma batalha sem imagem.
- **B1b (novo, necessário): gravação e reprodução de entrada.** Sem imagem, ninguém navega pelos menus, nem no PC nem no Vita. O runtime vai gravar o controle quadro a quadro numa sessão com imagem (`--input_record`) e reproduzir sem gráficos (`--input_replay`). Verificação: com D3D12 e capturas de tela, a reprodução chega à batalha; depois a mesma coisa com a GPU nula, conferindo pelo salto de memória e de custo da batalha. No Vita, a fase de carregamento leva outro número de quadros, então o roteiro precisa de folgas largas entre os passos de menu.
- **B2 — feito no PC (2026-10-23).** Memória por software no runtime (`REXGLUE_SOFT_MMU`, desenho acima), validada com a GPU nula.
  - **Builds:**
    - ReXGlue: preset `win-amd64-softmmu`, saída `rexglue/out/win-amd64-softmmu`, instalação `rexglue/out/install/win-amd64-softmmu`;
    - Storm 2: preset `win-amd64-softmmu`;
    - para rodar: `.\scripts\run_storm2_stats.ps1 -Build softmmu -Extra '--gpu_backend=null'`. O mesmo `-Build` existe em `capture_storm2.ps1` e `replay_session.ps1`.
    - O runtime exporta `REX_GUEST_SOFT_MMU` para quem o usa, e `Runtime::Setup` recusa código gerado no outro modelo de memória.
  - **Testes:** [V]
    - novos `tests/soft_mmu` (12 casos: tabela = `GuestPtr`, bloco contíguo, página sem acesso, visões A0/C0/E0, deslocamento do E0, alias 0x9/0x8, `Protect`, reserva sem acesso, `Zero`/`Fill`/`Copy` entre blocos, memória liberada, limite com guarda, tabela de funções): 12/12 em 10 rodadas seguidas (`docs/logs/E12_soft_mmu_tests.log`);
    - suíte normal: 3410/3411 (`docs/logs/E11_ctest_flat.log`; a falha é o bug 6, antiga). Os 1591 testes PPC no modo de tabela agora rodam com páginas de 4 KiB.
  - **Storm 2, 120 s, GPU nula, mesmo binário de código** (`docs/logs/E13a_flat_null_120s` × `E13b_softmmu_null_120s`): [V]

    | | arranjo plano | MMU por software |
    |---|---:|---:|
    | quadros/s | 30,0 | 30,0 |
    | instruções por quadro (mediana) | 25,6 M | 25,2 M |
    | desenhos / resolves a cada 600 quadros (depois do carregamento) | 72.600 / 18.600 | 72.600 / 18.600 |
    | memória do convidado comprometida (pico) | 479,9 MiB | 479,9 MiB |
    | erros no log | 115 × `NtQueryInformationFile` não implementado | os mesmos 115 |
    | escritas em memória liberada / MMIO por falha de página | — | 0 / 0 |

  - **Binários finais:** [V]
    - `E13f_softmmu_final_120s` repete a tabela: 30,0 quadros/s, 25,1 M instruções por quadro, 0 escritas em memória liberada.
    - A regressão de imagem do build normal com D3D12 (`check_storm2_render.ps1`, `E13g_render_flat`) passa. O interpretador de shader, usado por esse caminho, agora lê a memória por `TranslatePhysical`.
  - **Memória do host com a MMU por software** (`E13c`): [V]
    - 483 MiB em 569 blocos virtuais e 36 físicos, ou seja, toda a memória comprometida.
    - Os 3 blocos grandes somam 390 MiB: físico `0xAB000000` 272 MiB, virtual `0x40F40000` 70 MiB e físico `0xDC000000` 48 MiB.
    - Em seguida vêm `0xC0000000` (16 MiB), a imagem XEX (11,2 MiB) e blocos de 8 MiB ou menos.
  - **Limites:** [V]
    - `--soft_mmu_caps=0xAB000000=184` roda a tela de título com 394 MiB (`E13d`).
    - Com `0xAB000000=16` o jogo para no quadro 1 com a mensagem "wrote past the 16.0 MiB limit of physical region AB000000 … raise it with --soft_mmu_caps=0xAB000000=MiB" (`E13e`; o log tem duas rodadas, a segunda com o texto final).
  - **O que isso diz sobre o Vita:**
    - [V] O mecanismo funciona e nenhum acesso do jogo ou do runtime atravessa blocos nessas execuções.
    - [H] O limite por prefixo só reduz o bloco físico de 272 MiB. O virtual de 70 MiB tem um trecho usado no fim (`E7`), e o de 48 MiB é espalhado.
    - Conta de memória no Vita:
      - [V] 394 MiB de blocos com o limite de 184 MiB (medido na tela de título; os blocos fora dos 3 grandes somam ~66 MiB);
      - [H] mais ~40 MiB de código e tabelas no Vita (28,8 de código, 4,5 das tabelas de páginas, 6,6 da tabela de funções);
      - total ≈ **434 MiB**, se a batalha couber no mesmo limite.
    - Isso continua acima dos ~256 MB padrão. Reduzir exige limites de dois trechos (começo e fim) no bloco de 70 MiB, ou memória do convidado menor no próprio jogo. [P] Medir no console quanta RAM o app consegue (B3/B4).
  - **Fora da validação:** batalha (espera a gravação de entrada da B1b), D3D12 com MMU por software (recusado de propósito) e desempenho da tradução no PC (o jogo é limitado a 30 quadros/s; o custo no Vita já foi medido no benchmark).
- **Armadilha de build achada:** `rexglue codegen` reescreve `<pasta do manifesto>/generated/rexglue.cmake`. Com o manifesto do Vita em `storm2/`, o build do PC passou a compilar a cópia do Vita sem aviso. O manifesto do Vita agora fica em `storm2/vita/`, com saída em `storm2/vita/generated/`.
- **B3 — camada do Vita compila e passa no Vita3K (2026-10-26); falta o console.**
  - **Build:** projeto próprio em `vita/runtime` (não o CMake completo do ReXGlue, que puxa SDL3, D3D12, FFmpeg e o gerador). Ele compila com o VitaSDK as mesmas fontes de core, system, kernel e filesystem do build do PC, com `REXGLUE_SOFT_MMU`. [V] 0 erros e 0 avisos: `librexcore_vita.a` tem 2,2 MB e `librexruntime_vita.a` 7,5 MB.
  - **Camada de plataforma** (`rexglue/src/core/*_vita.cpp`):

    | Peça | No Vita |
    |---|---|
    | relógio | `sceKernelGetProcessTimeWide` (µs), hora do RTC |
    | memória | blocos `sceKernelAllocMemBlock`, zerados; endereço escolhido é recusado; sem proteção de página; reserva sem acesso devolve endereço baixo não mapeado |
    | threads | derivado do POSIX, sem sinais (ver abaixo); id, prioridade e afinidade pelo `SceUID` (hardware threads 0–5 do Xenon → núcleos 0–2) |
    | fibers | `SceFiber` (`sceFiberRun`/`Switch`/`ReturnToThread`) |
    | arquivos | `sceIo` direto (caminhos `ux0:`/`app0:`); "arquivo mapeado" = lido para a memória e gravado de volta |
    | exceções/SEH | o Vita não entrega falhas ao app: os tratadores ficam registrados e nunca rodam (escrito no log) |
    | bibliotecas dinâmicas | não existem: `Load()` falha com mensagem |
    | sockets | `close()`; `ioctl` falha (`ENOSYS`) |

  - **Threads sem sinais:**
    - os callbacks (APCs) só entram na fila; as esperas alertáveis já verificam a fila a cada 1 ms;
    - suspender ou terminar *outra* thread vale na próxima espera, pausa (`sleep`) ou `yield` dela, e isso vai para o log;
    - o Storm 2 chama `NtSuspendThread` 8 vezes, com o handle guardado num objeto (provavelmente outra thread); confirmar em B4.
  - **Correções para 32 bits** (bugs 24–25):
    - hash FNV-1a com constantes de 64 bits truncadas;
    - deslocamentos `>> 32` em `size_t`;
    - cabeçalho do heap do CRT;
    - `timegm` e `endian.h` ausentes;
    - `flockfile` ausente no newlib (`vita/runtime/compat`).
  - **Testes** (`dist/rexglue_runtime_tests.vpk`, título `RXGT00003`; log em `ux0:data/storm2port/runtime_tests.log`): 26 casos com as bibliotecas reais:
    - relógio, memória do host;
    - threads, eventos, semáforo, mutante, `WaitMultiple`, timer, TLS, id/prioridade/afinidade, callback alertável;
    - suspensão adiada, autossuspensão, término adiado;
    - fibers, arquivos, `std::filesystem`;
    - os casos da MMU por software, inclusive uma região de 272 MiB limitada a 184 MiB;
    - log.
  - **Vita3K** (`docs/logs/F1_runtime_tests_vita3k`): [V] 25/26.
    - A falha é `SetLength`: o Vita3K não implementa `sceIoChstatByFd`.
    - O emulador informa 224 MiB livres para o app no início; com a região de 184 MiB alocada sobram 19 MiB.
    - Os tempos do emulador não valem para o console.
  - **[P] No console:**
    - os 26 casos;
    - memória livre real;
    - `sceIoChstatByFd`;
    - datas do log (no Vita3K o spdlog marca 1965);
    - e, com TRIÂNGULO no fim, a prova de que a página sem acesso falha de fato (o app deve fechar com erro).
  - **Ainda não compilado para o Vita:** UI, áudio, entrada e o plugin de GPU nula (B4). O executável de teste só linka porque descarta seções não usadas (`--gc-sections`).
- **B4 — o Storm 2 roda no Vita3K sem gráficos (2026-10-26); falta o console.**
  - **Build** (`vita/storm2`, que inclui `vita/runtime`):
    - o código gerado (112 arquivos) mais o runtime do Vita: GPU nula, entrada pelo `sceCtrl`, áudio, e o FFmpeg (libavutil e libavcodec em C genérico, configuração gerada por `vita/runtime/tools/make_ffmpeg_config.py`);
    - saída `storm2_vita.vpk`: título `RXGT00006`, nome "Storm 2 (headless)", memória estendida (`ATTRIBUTE2=12`), 17 MiB;
    - código ARM 34,4 MB (`text`), dados e bss 1,5 MB; compilação completa ~22 min com 11 processos.
  - **O app** (`vita/storm2/src/main_vita.cpp`):
    - tela amarela ao iniciar, azul com o jogo rodando, vermelha com erro (a mensagem vai para os logs) e verde no fim;
    - START + SELECT por 2 s encerram;
    - arquivos em `ux0:data/storm2port`: `game/` (o jogo), `storm2.toml` (cvars opcionais), `storm2.log`, `storm2_stats.log` (a cada 5 s), `storm2_threads.log` (a cada 30 s), `frame_stats.csv`, `import_stats.csv`;
    - limite padrão `soft_mmu_caps=0xAB000000=184` (o `storm2.toml` pode trocar);
    - heap do newlib de 48 MiB.
  - **Áudio:** o XMA é decodificado de verdade e a saída é descartada. Um relógio consome um bloco de 256 amostras a cada 5333 µs, como o dispositivo SDL do PC, para o jogo andar no ritmo do áudio real. O log mostra quantos blocos por segundo o jogo entrega (tempo real = 187,5).
  - **Entrada:** botões e analógicos do Vita viram o controle do Xbox (Cruz/Círculo/Quadrado/Triângulo = A/B/X/Y, L/R = LB/RB, START/SELECT = Start/Back). A reprodução de entrada da B1b funciona no Vita (`input_replay` no `storm2.toml`).
  - **Correções:** bugs 26–29 em `docs/bugs.md`. A principal: os 320 contextos XMA criavam o decodificador do FFmpeg na partida (~190 MiB do host); no Vita o app abortava em 1 s.
  - **Vita3K** (`docs/logs/F3_storm2_vita3k`): [V no emulador]

    | | Vita3K (ARM32) | PC (`F4`, x64) |
    |---|---:|---:|
    | desenhos / resolves a cada 600 quadros, depois do carregamento | 72.079 / 18.600 | 72.600 / 18.600 |
    | blocos do convidado no host | 393,9 MiB (limite de 184 MiB) | — (arranjo plano) |
    | memória do convidado comprometida | 479,9 MiB | 480,0 MiB |
    | erros no log | os mesmos 115 `NtQueryInformationFile` | 115 |
    | escritas em memória liberada / além do limite | 0 / 0 | — |
    | heap do newlib em uso | 28,4 MiB de 48 | — |
    | blocos de áudio entregues pelo jogo | 90–128/s no 1º minuto; depois 58–84/s, média 71 (`F6`) | ~178/s |

    - **Reprodução de entrada** (`docs/logs/F5_replay_start`, START nos quadros 2500–3700): o jogo sai da tela de título nos dois, com os mesmos números: quadro 3000 → 121.580 desenhos no Vita3K e 122.280 no PC; quadro 3600 → 201.068 e 203.140; quadro 4200 → **238.860 desenhos e 24.600 resolves nos dois**. O código ARM32 percorre o mesmo caminho do x64, inclusive na resposta à entrada.
    - Execução do usuário no Vita3K (`docs/logs/F6_vita3k_340s`, 340 s, 7.124 quadros, fechada pelo emulador sem erro): mesmo padrão de 72.065–72.135 desenhos e 18.600 resolves por 600 quadros na tela de título, 393,9 MiB de blocos, heap estável em 28,4 MiB, os mesmos 115 erros. Depois do 1º minuto: 21,4 quadros/s e 71 blocos de áudio por segundo em média, ou seja, ~38 % do tempo real no áudio contra ~71 % nos quadros. No emulador, o motor de áudio do jogo é o que mais fica para trás.
    - Estabilidade: uma execução passou de 11 min (11.790 quadros) sem cair. O log dela não foi guardado: por engano, duas instâncias do Vita3K escreviam no mesmo arquivo (instalar pelo VPK reabre o emulador com outro processo; para rodar de novo, `Vita3K.exe -r RXGT00006`).
    - `NtSuspendThread` (8 chamadas) com a suspensão adiada da B3: o jogo segue igual ao PC.
    - O que o Vita3K **não** mostra: velocidade (6–7× mais rápido que o console); falta de memória (ele relata 0 KiB livres e continua alocando, então os blocos de reserva nunca são usados); tempo de CPU por thread (`runClocks` vem sempre 0).
  - **Conta de memória para o console [H]:**
    - pedido: 394 MiB de blocos do convidado (com o limite) + 4,5 MiB de tabelas de página + 48 MiB de heap + ~36 MiB do executável ≈ **482 MiB**;
    - no Vita3K, com o executável e o heap já carregados, sobram 317 MiB de RAM principal; mais 26 MiB de RAM contígua e ~110 MiB de CDRAM ≈ **453 MiB** para os blocos do convidado, contra 398 pedidos;
    - risco: os blocos de 184, 70 e 48 MiB precisam, cada um, de um bloco contíguo do sistema, e o tamanho do maior bloco possível no console é desconhecido (o `memprobe` mede);
    - se faltar memória, o app para com a mensagem "Software MMU: out of host memory", que diz a região e quanto já estava alocado.
  - **[P] No console (B5):** se a tela fica azul; quadros/s reais com a GPU nula (o limite de CPU); blocos de áudio por segundo; memória livre e uso dos blocos de reserva; CPU por thread.

Fora desta fase, documentado como pendente: renderização (GXM), áudio (reescrita NEON e XMA), tela SIM/NÃO (pode entrar em B4 se custar pouco).

## Riscos [H]
- **Fences e leituras de volta da GPU:** a GPU nula escreve fences e interrupções. Se o jogo ler de volta dados gerados pela GPU (consultas de oclusão, memexport, resolves lidos pela CPU), ele recebe zeros. Isso pode mudar o caminho do jogo e precisa ser checado em B1.
- **Áudio sem decodificação XMA:** as threads de áudio do jogo podem esperar contextos XMA que nunca terminam. Se isso travar o jogo, B4 precisa de um decodificador real (FFmpeg do VitaSDK), com custo de CPU a medir.
- **Bibliotecas de terceiros** (fmt, spdlog, toml, xxhash) no GCC do VitaSDK: [V] compilam e rodam no Vita3K (B3); spdlog sem `fwrite_unlocked` e sem fuso horário.
- **Memória:** se o modo estendido não der RAM suficiente, a medida fica limitada a um trecho do jogo ou a limites menores nos blocos grandes.
