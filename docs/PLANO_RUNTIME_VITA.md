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

## Projeto da memória (MMU por software, páginas de 64 KiB)
- O código gerado já usa a tabela `base[endereço >> 16]` (`REX_GUEST_SOFT_MMU`, validada com 1588/1588 no Vita).
- **Cada alocação do convidado ganha um bloco contíguo de memória do host.** Alocações arredondadas e alinhadas a 64 KiB no runtime do Vita garantem que duas alocações nunca dividam uma página da tabela. Assim, todo acesso do runtime dentro de uma alocação continua contíguo no host, e as centenas de `TranslateVirtual` do kernel não mudam.
- **Visões que repetem a memória física** (`0xA0000000`/`0xC0000000`/`0xE0000000`): várias entradas da tabela apontam para o mesmo bloco. O deslocamento de 4 KiB da visão `0xE0000000` vira `+0x1000` na entrada, com 4 KiB de folga no fim de cada bloco físico.
- **Blocos grandes:** memória real só até um limite por bloco, e o resto aponta para uma **página-guarda**. Leituras ali dão zero, correto para memória reservada e não tocada. Uma escrita suja a guarda, e a verificação a cada quadro **para o programa com mensagem** (bloco e limite), sem corrupção silenciosa. Os limites iniciais saem das medidas de batalha, com margem.
- [H] Orçamento: ~43 MiB de blocos pequenos + prefixo do físico grande (~180 MiB em batalha, a medir) + ~32 MiB do virtual + 48 MiB do físico de 16 MB ≈ **300 MiB**, mais 35 MiB de código e tabelas. Passa dos ~256 MB padrão do Vita. [P] Verificar no console quanta RAM o app consegue com o modo de memória estendida (`ATTRIBUTE2` no `param.sfo`, usado por ports conhecidos), medindo com `sceKernelGetFreeMemorySize`. Se não bastar, a medida de CPU continua possível com um limite menor, até a página-guarda acusar.

## Etapas
Cada etapa só termina com a verificação indicada. Nada é dado como feito sem execução.

| # | Etapa | Verificação |
|---|---|---|
| B1 | **GPU nula** no ReXGlue: subclasse do `CommandProcessor` que reaproveita a leitura de PM4 da classe base, ignora desenhos e resolves, e executa registros, `EVENT_WRITE`/`MEM_WRITE`/fences, interrupções e `XE_SWAP` | no PC, Storm 2 sem renderizar chega às batalhas, com o jogador jogando às cegas ou com entrada reproduzida: swaps a ~30/s, `import_stats` igual à execução com D3D12 |
| B2 | **Memória por software no runtime de PC**: a classe `Memory` com tabela de 64 KiB, alocações contíguas por bloco, prefixo + página-guarda nos blocos grandes | no PC: Storm 2 com `REX_GUEST_SOFT_MMU` (código gerado e runtime) e GPU nula roda igual a B1; `check_storm2_render.ps1` com D3D12 também passa |
| B3 | **Camada de plataforma do Vita** (`*_vita.cpp`): threads (pthread do VitaSDK ou `sceKernel*Thread`), fibers (`SceFiber`), relógio, arquivos (`sceIo`), blocos de memória; sem tratador de exceção (o MMIO já é tratado no código gerado), sem bibliotecas dinâmicas, sem sockets, sem SDL | `core`/`system`/`kernel`/`filesystem` compilam com o VitaSDK; testes unitários portáveis rodando no Vita |
| B4 | **App `storm2` do Vita**: liga runtime, GPU nula e o código gerado (`vita/storm2`); dados do jogo em `ux0:data/storm2port/game`; entrada por `sceCtrl`; áudio desligado e registrado como desligado (sem decodificação XMA nesta fase); log de quadros/s, memória livre e CPU por thread | o VPK abre, chega ao menu e à batalha, e grava o log de medidas |
| B5 | **Medida no console** | log do usuário: quadros/s de CPU em batalha, memória usada, threads |

### Andamento
- **B1 — feito no PC (2026-10-23).**
  - `rex::graphics::null` (`include/rex/graphics/null/null_gpu.h`) e `--gpu_backend=null` (`--gpu_backend` é novo no `ReXApp`). Sem renderizar, o Storm 2 roda a 29,9 quadros/s, com ~121 desenhos e 31 resolves por quadro, os mesmos números do D3D12, e 25,4 M instruções por quadro (mediana; 30,8 M com D3D12, por causa de menos espera ativa pela GPU). Log: `docs/logs/E10_nullgpu_120s`.
  - **Falta** chegar a uma batalha sem imagem.
- **B1b (novo, necessário): gravação e reprodução de entrada.** Sem imagem, ninguém navega pelos menus, nem no PC nem no Vita. O runtime vai gravar o controle quadro a quadro numa sessão com imagem (`--input_record`) e reproduzir sem gráficos (`--input_replay`). Verificação: com D3D12 e capturas de tela, a reprodução chega à batalha; depois a mesma coisa com a GPU nula, conferindo pelo salto de memória e de custo da batalha. No Vita, a fase de carregamento leva outro número de quadros, então o roteiro precisa de folgas largas entre os passos de menu.
- **Armadilha de build achada:** `rexglue codegen` reescreve `<pasta do manifesto>/generated/rexglue.cmake`. Com o manifesto do Vita em `storm2/`, o build do PC passou a compilar a cópia do Vita sem aviso. O manifesto do Vita agora fica em `storm2/vita/`, com saída em `storm2/vita/generated/`.

Fora desta fase, documentado como pendente: renderização (GXM), áudio (reescrita NEON e XMA), tela SIM/NÃO (pode entrar em B4 se custar pouco).

## Riscos [H]
- **Fences e leituras de volta da GPU:** a GPU nula escreve fences e interrupções. Se o jogo ler de volta dados gerados pela GPU (consultas de oclusão, memexport, resolves lidos pela CPU), ele recebe zeros. Isso pode mudar o caminho do jogo e precisa ser checado em B1.
- **Áudio sem decodificação XMA:** as threads de áudio do jogo podem esperar contextos XMA que nunca terminam. Se isso travar o jogo, B4 precisa de um decodificador real (FFmpeg do VitaSDK), com custo de CPU a medir.
- **Bibliotecas de terceiros** (fmt, spdlog, toml, xxhash) no GCC do VitaSDK: devem compilar, mas não foi testado.
- **Memória:** se o modo estendido não der RAM suficiente, a medida fica limitada a um trecho do jogo ou a limites menores nos blocos grandes.
