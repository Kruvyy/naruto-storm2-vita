# Relatório de viabilidade — Naruto Shippuden: Ultimate Ninja Storm 2 no PS Vita

Data: 2026-10-23. Base: medidas reais no PC (Ryzen 5 4500, RX 6600) e no PS Vita físico do usuário. Legenda: **[V]** verificado por execução; **[H]** hipótese ou estimativa; **[P]** pendente.

O plano do projeto condiciona os marcos E–H (assets reais, renderização no Vita, tela SIM/NÃO, VPK com batalha) a dois portões: **memória** (portão 1) e **desempenho de CPU** (portão 2). Este relatório reúne os números dos dois e da GPU, e diz o que seria preciso para seguir.

## 1. Veredito

| Frente | Situação | Veredito |
|---|---|---|
| Tradução do código (ReXGlue → ARM32) | funciona e compila inteiro | **ok** [V] |
| CPU (portão 2) | lógica do jogo a ~16–17 quadros/s a 333 MHz; áudio não cabe sem reescrita | **desfavorável para 30 quadros/s**; ~15–20 parece alcançável com trabalho grande [H] |
| Memória (portão 1) | ~320 MiB antes do runtime e das cópias de GPU, contra 256 MB + 128 MB | **não cabe do jeito direto**; cabe só com texturas reduzidas e outras medidas [H] |
| GPU | não existe renderizador para o Vita | **sem dados**; é o maior bloco de trabalho e o maior risco [P] |

Em resumo: um port **nativo a 30 quadros/s com fidelidade total** não é viável com a abordagem atual, pelas medidas disponíveis. Um port **jogável abaixo de 30 quadros/s, com texturas reduzidas**, não está descartado, mas depende de três trabalhos grandes ainda não começados: runtime do Vita, renderizador GXM e reescrita do áudio. O resultado só se confirma medindo no console.

## 2. O que já está verificado [V]

| Item | Evidência |
|---|---|
| Recompilador validado | 3410/3411 testes no PC (a falha é um teste de ferramenta no Windows, item 6); **1588/1588 no Vita físico** (`docs/logs/C17_vita_fisico_ppc_tests.log`) |
| Storm 2 roda no PC pelo código recompilado | imagem correta, batalhas jogadas pelo usuário (695 s sem travar, `docs/logs/batalha2`) |
| Bugs de tradução e runtime achados e corrigidos com teste | itens 17 (funções por constante de endereço), 19 (tela preta), 20 (tabelas de função viravam `switch` e travavam a batalha), 21 (modelo de registros) em `docs/bugs.md` |
| Imports | 229 no XEX, 115 usados em execução, **nenhum** stub alcançado (`docs/imports.md`) |
| Código do jogo compila para o Vita | **112/112 arquivos**, GCC 15.2 ARMv7, **28,8 MiB** de código (`vita/storm2`) |

## 3. CPU (portão 2)

### Medidas
**No PC, batalha** (`docs/logs/batalha2`, `batalha3`; amostragem por thread, `scripts/profile_guest_thread.ps1`):

| Thread | O que faz | Instruções PPC/s | Observação |
|---|---|---:|---|
| `F8000068` | renderização | 495 M (contadas) | quase tudo é **espera ativa pela GPU** (`sub_826A5DC0`/`sub_826ADDE0`), não trabalho |
| `F80000B0` | áudio [H, evidência forte] | 169 M | **80 % do tempo em 2 funções** (`sub_82599010`: float com `fmadds`; `sub_8281E500`: decodificador) |
| principal | lógica do jogo | 127 M | espalhada: 44 funções = 50 % do tempo |
| `F8000074/78/7C` | trabalho auxiliar | 26 M cada | quase ociosas |

**No Vita físico** (`docs/logs/E1_bench/bench_vita_fisico.log`; o sistema informou **ARM 333 MHz**):

| Tipo de código | Instruções PPC/s |
|---|---:|
| formato de lógica de jogo (`struct`), modelo de registros v1 em uso no Storm 2 | **68** |
| inteiro sintético | 63–66 |
| ponto flutuante `fmuls`/`fadds` | 23 |
| ponto flutuante com `fmadds` (FMA exata por software) | **~8** |

### Estimativas [H]
- **Lógica do jogo:** 127 M instr/s necessários contra ~68 M/s por núcleo ⇒ **~16–17 quadros/s** a 333 MHz. Com overclock real de 500 MHz (não confirmado para o app), algo como 20–25, já que memória e barramento não sobem junto.
- **Áudio:** com o laço quente cheio de `fmadds` a ~8 M/s, a thread recompilada **não cabe** nem em vários núcleos. A reescrita nativa (NEON) das duas funções quentes é **obrigatória**. A decodificação XMA, que no PC usa a `libavcodec`, também vai custar CPU no Vita [P].
- **Renderização:** a espera ativa precisa virar espera bloqueante (tratar em alto nível o fence do D3D), senão ela come um núcleo inteiro.
- Opções do codegen: só a v1 é segura no Storm 2 (+16 % no `struct`); as v2/v3 travam o jogo (bug 21) e renderiam só mais ~3–5 %.

## 4. Memória (portão 1)

### Medidas [V]
| Parte | MiB |
|---|---:|
| Código ARM do jogo | 28,8 |
| Tabela de despacho indireto | ~6,4 |
| Memória do convidado residente em batalha | **285–311** (física 192–208 + virtual 93–103) |
| — dessa memória física, usada pela GPU | **~67 a cada 10 s; 136 desde o boot** (texturas 108) |
| Memória física que o jogo reserva (comprometida) | 348 |

### Leitura [H]
- O Vita tem **256 MB de RAM principal e 128 MB de CDRAM**. Só o código mais o convidado dá ~320 MiB, antes do runtime e das cópias de GPU.
- A GPU do Vita lê a RAM principal mapeada, então os dados de GPU não precisam mudar de lugar. O problema é que as texturas do Xbox (ladrilhadas, em formatos próprios) precisam de **cópias convertidas** para o GXM: mais ~70–110 MiB.
- **O que faria caber**, sempre alterando um pouco o jogo e precisando de prova no console:
  1. texturas convertidas em **metade da resolução** (1/4 da memória);
  2. **alocação sob demanda** na MMU por software (o jogo reserva 348 MiB e toca 192);
  3. alvos de renderização em **960×544** (57 % dos pixels de 1280×720).

## 5. GPU [P]
Não existe nada para o Vita. Seria preciso:
- portar o processador de comandos e o modelo de EDRAM/resolves do ReXGlue;
- traduzir os shaders Xenos para shaders do GXM (hoje o ReXGlue gera só DXBC e SPIR-V);
- converter texturas (ladrilhamento e formatos) e gerenciar alvos de renderização na CDRAM.

O custo de GPU no Vita (SGX543MP4+) contra o Xenos é **desconhecido**: nada foi medido. No PC a cena de batalha tem ~150 desenhos e ~31 resolves por quadro, com HDR (`16_16_16_16_FLOAT`) e pós-processamento.

## 6. O que seria preciso para seguir

Em ordem. Cada etapa termina com uma medida que decide se vale a próxima.

| # | Etapa | Entrega e medida decisiva | Tamanho [H] |
|---|---|---|---|
| 1 | **Runtime mínimo do Vita** (threads, eventos baratos, arquivos em `ux0`, memória sob demanda, controle, relógio) e boot **sem gráficos** | o jogo recompilado roda no console até a lógica de batalha; **quadros/s reais de CPU e memória reais no Vita** | grande |
| 2 | **Áudio nativo**: reescrita NEON das 2 funções quentes, com teste de equivalência contra a recompilada; medir a decodificação XMA | thread de áudio cabendo num núcleo | médio |
| 3 | **Espera pela GPU em alto nível** e correção do bug 21, para pegar mais 3–5 % | núcleo da renderização livre | pequeno |
| 4 | **Renderizador GXM** (comandos, shaders, texturas reduzidas, alvos a 960×544) | primeira imagem no Vita; **custo de GPU por quadro** | muito grande |
| 5 | Entregas do briefing: tela SIM/NÃO, VPK, batalha Naruto vs Sasuke na arena | VPK instalável com batalha real | médio |

A tela SIM/NÃO sozinha é pequena e pode ser feita a qualquer momento, mas o briefing não aceita tela preta ou arena estática como batalha.

## 7. Opções para decidir

| Opção | O que é | Prós | Contras |
|---|---|---|---|
| **A. Port completo** | etapas 1–5 | único caminho para o objetivo do briefing | meses de trabalho; resultado provável abaixo de 30 quadros/s e com texturas reduzidas; risco alto na GPU |
| **B. Prova no console (recomendada)** | etapa 1 (+3): o jogo roda no Vita sem gráficos | troca estimativas por medidas reais de CPU e memória antes de investir na GPU | ainda não mostra imagem |
| **C. Pausar** | parar aqui com tudo documentado | nenhum custo adicional | o objetivo fica em aberto |

**Recomendação:** opção **B**. Ela é a mais barata entre as que respondem a pergunta que falta: o jogo de verdade, no Vita de verdade, quantos quadros por segundo de CPU entrega e quanta memória ocupa. Se der menos de ~10 quadros/s mesmo com o áudio fora da conta, o port completo não se justifica. Se der perto de 15–20, vale seguir para o áudio e a GPU.

## Referências
- Estado detalhado e todas as tabelas: `docs/STATUS.md`
- Bugs e correções: `docs/bugs.md`; imports: `docs/imports.md`
- Logs: `docs/logs/` (batalhas: `batalha2`, `batalha3`, `E6_batalha`; Vita: `C17_vita_fisico_ppc_tests.log`, `E1_bench/bench_vita_fisico.log`)
