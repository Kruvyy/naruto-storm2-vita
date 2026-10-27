# Plano — renderizador do Storm 2 para o PS Vita (fase G)

Data: 2026-10-26. Objetivo: mostrar a imagem do jogo no Vita, primeiro no Vita3K. Legenda: [V] verificado; [H] hipótese; [P] pendente.

## Fatos que definem o projeto
1. **Os renderizadores do ReXGlue não servem no Vita.** [V] D3D12 e Vulkan leem vértices e texturas dentro do shader (a memória inteira do jogo exposta como buffer) e convertem texturas e a EDRAM com compute shaders. A GPU do Vita (GXM) não tem compute shaders nem buffers de leitura arbitrária. No Vita, vértices e texturas precisam ser convertidos na CPU e entregues como atributos e texturas comuns.
2. **Shaders compilados no aparelho exigem o `libshacccg.suprx`.** [V] (`docs/logs/G0_gltest_vita3k`) O vitaGL (r1488) e o vitaShaRK (1.7) compilam e linkam, mas no Vita3K o compilador não inicia (`0x80010002`, arquivo ausente) e o `vglInit` falha. O arquivo é da Sony: cada usuário extrai do próprio console (ShaRKBR33D) e ele fica em `ur0:data/external/libshacccg.suprx`. Não pode ser baixado nem distribuído por nós. [P] Copiar o do console do usuário para o Vita3K e repetir a G0.
3. **O que o jogo desenha** (`--null_gpu_census_swaps`, `docs/logs/G1_census` = 5000 quadros com START reproduzido; `G1_census600` = 600 primeiros quadros): [V]
   - 66 shaders distintos (35 de vértice, 31 de pixel) até o menu; 29 nos 600 primeiros quadros;
   - só 1 shader de pixel com saltos/laços; nenhum com endereçamento dinâmico de registros ou constantes; 8 com `kill`, 1 escreve profundidade;
   - o índice de todo `vfetch` é `r0.x` (o índice do vértice): os vértices podem virar atributos comuns;
   - formatos de vértice: `32_32_FLOAT`, `32_32_32_FLOAT`, `32_32_32_32_FLOAT`, `16_16_FLOAT`, `16_16_16_16_FLOAT`, `8_8_8_8`;
   - primitivas: lista de pontos (30 %), lista de quadriláteros (34 %), tira de triângulos (26 %), lista de retângulos (10 %), lista de triângulos;
   - texturas: `8_8_8_8`, `DXT4_5`, `DXT1`, `5_6_5`, `4_4_4_4`, `8` (linear), `16_16_16_16_EXPAND` (HDR), `24_8` (profundidade lida como textura), um cubemap `DXT4_5`; todas ladrilhadas (tiled) menos a `8`;
   - alvos: `8_8_8_8` 1280 de largura (86 %), `16_16_16_16_FLOAT`, MSAA 4× com largura 640/520 (1280×720 lógico);
   - resolves para `8_8_8_8` (cor e profundidade) e `16_16_16_16_FLOAT`;
   - coordenadas já em tela (`PA_CL_VTE_CNTL 0x300`) em 40 % dos desenhos; 3D com transformação de viewport (`0x43F`) no resto.

## Arquitetura
Um backend novo, `rex::graphics::gles`, escrito sobre um subconjunto do OpenGL ES 2 (com framebuffer objects) que o vitaGL implementa. **O mesmo código roda no PC com OpenGL**, para desenvolver e comparar imagens com o D3D12 sem depender do Vita3K nem do console.

| Peça | Como |
|---|---|
| API | tabela de funções GL: no PC carregada pelo SDL3 (contexto num janela oculta), no Vita as funções do vitaGL |
| Shaders | `GlslShaderTranslator`: microcódigo do Xenos → GLSL simples (ES 1.00 no Vita, 1.20 no PC). Registros como `vec4 r[N]`, predicado como `bool`, `(p0)` como `if`; saltos e laços por uma máquina de estados só quando existem |
| Vértices | por fetch constant: copiar a faixa do buffer do jogo, trocar a ordem dos bytes, enviar como VBO; um atributo por `vfetch` |
| Índices | trocar bytes; lista de quadriláteros → triângulos; lista de retângulos → o 4º vértice calculado na CPU (exato se o shader de vértice for afim [H]); pontos → `GL_POINTS` |
| Constantes | `uniform vec4 c[256]` por estágio; bool/loop como uniforms |
| Viewport | `draw_util::GetHostViewportInfo` → escala/deslocamento NDC no shader de vértice (cobre `0x300` e `0x43F`) |
| Alvos de renderização | cada (base na EDRAM, largura, formato) vira uma textura + FBO do host na resolução lógica; MSAA ignorado (1 amostra); sobreposição de alvos na EDRAM ignorada [simplificação] |
| Resolves | cópia do alvo para uma "textura resolvida" ligada ao endereço de destino; não volta para a memória do jogo [H: o jogo pode ler alguma de volta] |
| Texturas | se o endereço tem textura resolvida, usa ela; senão desladrilha e converte na CPU (DXT fica comprimida quando o host aceita) e guarda em cache com hash dos bytes |
| Apresentação | no `VdSwap`, a textura resolvida do frontbuffer vai para a tela (Vita: 960×544, escalada); no PC, um PNG a cada N quadros para comparar com as capturas do D3D12 |

## Andamento (2026-10-26)
- **G1 no PC: o renderizador desenha o jogo.** [V] Backend `rex::graphics::gles` no ReXGlue (`src/graphics/gles/`, `src/graphics/pipeline/shader/glsl_translator.cpp`), selecionado com `--gpu_backend=gles`; no PC ele usa OpenGL de desktop num contexto oculto e grava o frontbuffer em PNG (`--gles_dump_dir`, `--gles_dump_interval`).
  - **Tradutor GLSL:** 65 dos 66 shaders do censo compilaram de primeira; o último tinha um conflito de nomes, já corrigido (`--gles_shader_test_dir` traduz e compila uma pasta de shaders despejados).
  - **Imagens** (PNG locais em `storm2/gles_frames/`, fora do git por serem derivadas do jogo):
    - a tela de aviso de salvamento automático, com texto, ícone e botão;
    - o logo da CyberConnect2 com a animação de brilho;
    - a abertura em 3D: Pain em close, Sakura na Konoha destruída, a cena do templo com personagens e rochas;
    - o logo "Naruto Shippuden Ultimate Ninja Storm 2".
  - **Velocidade no PC:** 29,6 quadros/s (igual ao D3D12) depois de copiar só os vértices usados e redecodificar uma textura só quando os bytes dela mudam. Antes disso: 3,8 quadros/s.
  - **Com a memória por software** (o modelo do Vita): 29,1 quadros/s e a mesma imagem (`docs/logs/G1_gles_softmmu`).
  - **Regressão:** suíte do PC 3410/3411 (a falha é o bug 6, antiga); D3D12 continua com imagem (`docs/logs/G1_render_d3d12`, `check_storm2_render.ps1` passa).
  - **Ferramentas novas:** `--null_gpu_census_swaps`/`--null_gpu_dump_shaders` (censo e despejo de shaders); `--gles_trace_swap=N` (cada desenho e resolve de um quadro, com alvo, viewport, blend e texturas; `docs/logs/G1_gles_trace150b`); `--gpu_swap_diag_png_dir` (frontbuffer do D3D12 em PNG, mesma numeração de quadros, para referência).
- **Erros achados no caminho:**
  - o `GetHostViewportInfo` com origem no topo já entrega o y "para baixo", que coincide com os alvos do host; o tradutor invertia o y outra vez e a cena aparecia deslocada (corrigido);
  - o contador de programa do GLSL tinha o mesmo nome das constantes do shader de pixel (corrigido).
- **O que ainda está errado ou falta (G2):**
  - **MRT:** o jogo grava em dois alvos de cor de uma vez (o segundo na base 640 da EDRAM). O OpenGL ES 2 e a GXM têm um alvo só, então o segundo fica com conteúdo antigo. Ele entra numa soma de duas texturas e deixa cópias acumuladas na tela de salvamento. No Vita a saída será uma segunda passagem do mesmo desenho;
  - **EDRAM compartilhada:** alvos com a mesma base e formatos diferentes são texturas separadas aqui, mas na EDRAM são a mesma memória;
  - **profundidade como textura** (resolves de profundidade, formato `k_24_8`): sai preta;
  - **cubemaps:** sai preto;
  - **HDR no Vita:** os alvos `16_16_16_16_FLOAT` viram RGBA8;
  - **atributos `half float` no vitaGL:** não verificados;
  - **MSAA:** ignorado, uma amostra.
- **G1e: primeira imagem do jogo no Vita3K** (rexglue `012ef24`; emulador, não o console). [V] Com o `libshacccg.suprx` do console do usuário em `ur0:data/external/` do Vita3K, o app (`vita_gpu=auto`) usa o renderizador e mostra a tela de carregamento "AGUARDE 0…100%" igual à do PC (capturas locais em `storm2/captures/vita3k/`, fora do git). Logs: `docs/logs/G1e_vita3k`.
  - **O que impedia a imagem** (cada item achado com um teste, todos corrigidos):
    1. o retorno de `vglInitWithCustomSizes` não é sucesso/falha (é `GL_TRUE` só quando a resolução foi reduzida); a checagem certa é `is_shark_online`;
    2. framebuffer só de profundidade é "incompleto" no vitaGL (0x8CD7): ganhou um anexo de cor descartável, um por tamanho;
    3. `glCopyTexSubImage2D` no vitaGL é um `glReadPixels` (espera a GPU inteira) mais um novo upload, a cada resolve (~30 por quadro). No Vita3K a leitura volta zerada. Os resolves agora são um desenho num FBO sobre a textura de destino: no PC a imagem saiu idêntica byte a byte, e no Vita3K o jogo foi de 0,2 para até 15 quadros/s;
    4. o vitaGL devolve como posição de uniform o endereço negado de um registro interno, que pode ser negativo; só −1 significa "não existe";
    5. com o teste de tesoura ligado, o vitaGL descarta os desenhos num FBO (`gles_present_test=6`). Agora não há tesoura do GL: o viewport vira a interseção com o retângulo da tesoura e a transformação NDC compensa, e as limpezas dos resolves são desenhos (o `glClear` do vitaGL cobre uma área do tamanho da tela);
    6. o `discard` das instruções *kill* do Xenos ficava dentro de `xe_body()`, em `if` aninhados, e matava todos os pixels dos shaders do jogo. Agora a instrução marca `xe_kill` e o único `discard` fica no `main()`. O teste isolado (célula 23) não reproduz a falha; a correção é empírica [H sobre a causa no compilador].
  - **Testes de construções GLSL no vitaGL** (`gles_present_test=5`, 24 células, Vita3K): passam profundidade em −w e 0,999w, `bool` global, array de uniform com índice constante, varying escrito em função, `mat4 * vec4`, o epílogo de posição do Xenos, `discard` tomado e não tomado, o teste alfa do tradutor, um shader de fragmento em 3 programas e o shader de pixel 7ABDFEFB do jogo. **Falham:** array de uniform com índice calculado (lê zeros) e `for` com `continue`/`break`. Isso afeta o endereçamento relativo (`a0`/`aL`) e os shaders com saltos (modo `xe_cfpc`), ainda raros nas telas iniciais (1 dos 66 shaders do censo).
  - **Custo no Vita3K** [V no emulador, não vale para o console]: compilar é instantâneo (o vitaGL adia para a ligação); ligar um programa leva 0,8–4,8 s, porque o `libshacccg` roda emulado.
  - **Para depois dos ~100 s** [V no emulador]: o Vita3K cai dentro do `libshacccg` ao compilar um shader da abertura em 3D, com o heap do newlib cheio (48 MiB) e `free_user=0`. Causa provável [H]: o vitaGL cria um buffer de profundidade privado para cada FBO (5,2 MiB a 1280×1024, mais o stencil), e o backend cria muitos FBOs (pares cor/profundidade, um por destino de resolve, anexos descartáveis). É o próximo trabalho (G1f): menos FBOs, alvos com a altura real em vez de 1024 e um orçamento de memória da GPU.
  - **Diagnóstico** (cvars no `storm2.toml`): `gles_present_test` (1 limpeza, 2 textura da CPU, 3 maior alvo do quadro, 4 render-to-texture, 5 construções GLSL, 6 layout de FBO), `gles_debug_solid_ps`, `gles_debug_ps_patch` (`opaque`, `alpha`, `atest`, `i0`, `i1`, `nodiscard`), `gles_report_interval` (tempos de tradução/compilação/ligação/apresentação e o maior quadro), `gles_trace_swap` (agora com o estado do teste alfa). Erros do GL entram no log.
  - **Ferramenta:** `scripts/capture_vita3k.ps1 -Saida <png>` captura a janela do Vita3K.
- **G1f: memória da GPU no vitaGL** (rexglue `fefc455`; emulador). [V] O jogo passa do ponto em que caía (~100 s, heap do newlib cheio dentro do `libshacccg`) e segue até o início da abertura (~quadro 520–565). Logs: `docs/logs/G1f_vita3k`.
  - **Relatório de memória** (`gles_report_interval`): no PC a abertura ocupa ~90–140 MiB; no Vita3K, ~76–99 MiB (alvos 14, resolves 25–29, texturas 11–27, profundidade escondida do vitaGL ~26–29, estimada).
  - **O que reduziu:** texturas e resolves sem uso por 300 quadros são liberados; alvos começam com 736 linhas e crescem se um resolve pedir mais; dois FBOs por tamanho de destino de resolve, alternados (era um por textura); passes só de profundidade num alvo de rascunho 16×16 no vitaGL; pools do vitaGL CDRAM 100 MiB e phycont 16 MiB.
  - **Erros do vitaGL achados no caminho** (corrigidos):
    1. *culling* GL_FRONT ou GL_FRONT_AND_BACK liga o modo "sem polígonos" do vitaGL: todo desenho de triângulo seguinte é descartado antes de começar a cena, o splash do vitaGL não sai e as trocas de quadro são ignoradas (o logo piscando que o usuário gravou). Agora só GL_BACK: culling da frente = culling de trás com a ordem invertida; as duas faces = nada a desenhar. O splash termina logo depois da inicialização;
    2. trocar a textura de um FBO cuja cena o vitaGL mantém aberta estraga o trabalho da GPU (tela congelada): por isso os FBOs de resolve se alternam;
    3. `vglSetUniformData` escreve quantos elementos e componentes receber, sem limite, em buffers que o vitaGL aloca primeiro no heap do newlib. Arrays de constantes agora vão até o tamanho que o programa compilado manteve (`glGetActiveUniform`), vec4 com o número de componentes mantido, e os shaders auxiliares usam vec2/float.
  - **Tentativas que quebram no Vita3K** (desligadas, com cvar para estudo): um FBO por alvo de cor (tela preta) e pular os passes só de profundidade (a tela alterna com a imagem do splash). Causa não encontrada.
  - **Configuração do Vita3K:** com `disable-surface-sync: true` a imagem do jogo fica branca. Os testes usam surface sync ligado e resolução 1×.
  - **Próximo problema** [V no emulador, causa H]: no início da abertura o emulador cai na camada de threads: leitura inválida em `pte_osTlsGetValue` (estado de uma thread já inválido) e depois um `std::thread` começa de um estado destruído (PC=0 em `execute_native_thread_routine`). As execuções com GPU nula passaram de 7.000 quadros sem isso, então algo da renderização muda o tempo ou a memória das threads. As pilhas das XThreads no Vita agora são limitadas a 2 MiB (pediam 16 MiB). → Resolvido em G1g: não eram as threads.
- **G1g: a queda no início da abertura** (rexglue `3e04aa4`; emulador). Logs: `docs/logs/G1g_vita3k`, `docs/logs/G1g_pc`.
  - **Causa** [V]: o `vita3k.log` mostra `terminate called after throwing an instance of 'std::bad_alloc'` logo antes das quedas. Um `new` de 1280×720×4 bytes falhou, o `abort` do newlib chama `_exit`, que libera o bloco do heap antes de encerrar o processo, e as outras threads caem lendo memória liberada (as quedas em `pte_osTlsGetValue` e `execute_native_thread_routine` eram isso). O heap enchia porque o vitaGL, com os pools cheios, aloca no heap do newlib (`vglUseExtraMem`, ligado por padrão): no filme da abertura o heap foi de 33 para 48 MiB.
  - **O filme**: três planos 1280×720 `k_8` (Y, U e V, novos endereços a cada filme) mais duas texturas 1280×720 `k_4_4_4_4`, decodificados a cada quadro. Cada plano virava RGBA (3,6 MiB) e um `glTexImage2D` novo; o vitaGL dá armazenamento novo a cada chamada e libera o antigo só depois da GPU.
  - **Correção**: `k_8` e `k_8_8` vão como `GL_LUMINANCE`/`GL_LUMINANCE_ALPHA` (900 KiB por plano), com o swizzle remapeado para ler RRRR e RGGG, como na tabela de formatos do Xenia (antes saíam com 0 em G/B e 1 em A; `k_5_6_5` agora repete o azul no alfa, RGBB); textura com mesmo tamanho e formato é atualizada no lugar (`glTexSubImage2D`); a conversão escreve direto num buffer reaproveitado (até 4 MiB); `gles_vita_extra_mem` (padrão desligado) impede o vitaGL de usar o heap do newlib; `GL_TEXTURE_MAX_LEVEL` só no desktop (no vitaGL dava `GL_INVALID_ENUM` a cada quadro e escondia outros erros). O app do Vita troca o `operator new`: uma falha registra tamanho, uso do heap e pilha em `storm2_stats.log` e para com mensagem, sem liberar o heap; a linha de estatísticas ganhou `heap_free` e `heap_arena`.
  - **Resultado** [V no emulador]: heap estável (~31–32 MiB) durante o filme da CyberConnect2, que aparece com as cores certas. No PC os quadros 150–450 saem idênticos aos de antes e os filmes aparecem certos.
  - **Desempenho no Vita3K** [V no emulador]: o filme roda a ~0,3 quadro/s com o renderizador quase parado (traduzir 1 ms, apresentar ~0,4 s em 33 s): o tempo vai no decodificador de WMV do próprio jogo, código PPC recompilado. A abertura em 3D para ~3,5 min ligando shaders no `libshacccg` emulado.
  - **Próximo problema** [V no emulador]: o próprio Vita3K cai (escrita no endereço 0x20 do host) ~28 s depois de abrir `Dolby.wmv` (bug 47).
- **G1h: desempenho — cache de shaders e filmes** (rexglue `cfd4cfa`, port `8281c96`; emulador e PC). Logs: `docs/logs/G1h_vita3k`, `docs/logs/G1h_pc_*`.
  - **Cache de shaders** (`gles_shader_cache_dir`; o app do Vita usa `ux0:data/storm2port/shader_cache`): depois de ligar um programa, o par GXP compilado (`glGetProgramBinary` do vitaGL) vai para `<chave>.gxp`, com a chave = hash dos dois GLSL, dos atributos e do nível de otimização. Nas execuções seguintes o programa é refeito do GXP com shaders Cg (`glShaderBinary` + `glBindAttribLocation` + `glLinkProgram`), sem o compilador. O `glProgramBinary` do vitaGL não é usado: ele soma de novo as ligações de atributos a cada carga e pode passar da tabela de 16 (lido no código, 2bdbe89). [V no emulador] os 11 programas da abertura em 3D: **224,6 s → 14 ms**; o jogo chega ao filme em 72 s em vez de ~300 s. Os arquivos derivam dos shaders do jogo: ficam no aparelho, nunca no repositório.
  - **Filmes** (`storm2_skip_movies`, padrão ligado no Vita): os 7 filmes (`logo_cc2`, `Dolby`, `opening`, `dm110`–`dm220`) são decodificados pelo decodificador WMV do SDK do Xbox ligado ao jogo, em código PPC (~0,2–0,3 quadro/s no Vita3K). Esconder os `.wmv` não serve (o jogo espera numa tela branca). O gancho em `sub_8255F820` (início de filme) não abre o filme e só repete o efeito colateral que o resto usa (o seletor de heap do alocador do jogo em 0x8288BAA4); o jogo vê o filme como terminado (`sub_8255F208`). [V no PC, D3D12] a tela de título aparece em ~30 s; [V no emulador] os três filmes pulados em menos de 1 s. Os filmes da história também são pulados; tocá-los de verdade pede decodificação nativa (por exemplo, converter para H.264 no PC e tocar com o decodificador do Vita), trabalho à parte.
  - **Memória do compilador** (bug 48): bloco próprio de 32 MiB para o `libshacccg`, fora do heap do newlib; pico medido de 18,3 MiB num programa do título.
  - **11 varyings** (bug 49): um programa da tela de título derruba o Vita3K ao compilar, reproduzido isolado com `gles_program_dump_dir`/`gles_program_test_dir`; com 10 varyings liga. Programas com mais de 10 varyings não são ligados no vitaGL (desenhos pulados): o jogo chega à tela de título no Vita3K, sem o fundo.
  - **O que ainda pesa** [V no emulador]: com o cache quente e os filmes pulados, o título carrega a ~0,2–0,4 quadro/s com o renderizador quase parado (CPU do jogo: carga e descompressão). Na primeira execução cada programa novo ainda compila (1,7–70 s no Vita3K). O0 compila mais devagar que O2 (10,1 s contra 2,8 s num programa).
  - **Novos problemas**: vermelho e azul trocados na tela de título no Vita3K (bug 50); fundos brancos no título e nos menus no GLES do PC (bug 51).

## Etapas
| # | Etapa | Verificação |
|---|---|---|
| G1a | esqueleto no PC: contexto GL, alvos, limpezas, resolves e apresentação | PNGs com as cores de limpeza nos quadros certos |
| G1b | tradutor GLSL | os 66 shaders do censo compilam no driver do PC sem erro |
| G1c | vértices, índices, constantes, viewport, blend/profundidade/stencil/culling | geometria visível nos PNGs |
| G1d | texturas e resolves como textura | logos, tela de título e menu parecidos com as capturas do D3D12 |
| G1e | o mesmo código no Vita (vitaGL) | imagem no Vita3K: **feito** (tela de carregamento, 2026-10-26) |
| G1f | memória da GPU no vitaGL (FBOs, alvos, texturas) e as construções GLSL que falham | memória: **feito** (passa do ponto de 100 s); abertura em 3D ainda cai na camada de threads |
| G1g | queda no início da abertura | **feito**: era o heap do newlib cheio pelo vitaGL (bug 46); passa pelo filme da CyberConnect2; o Vita3K cai no filme da Dolby (bug 47) |
| G1h | desempenho: cache de shaders, filmes | **feito**: cache (224,6 s → 14 ms na abertura), filmes pulados (opção), compilador fora do heap; tela de título no Vita3K sem o fundo (bug 49) e com cores trocadas (bug 50) |
| G2 | MSAA, HDR, profundidade como textura, alvos múltiplos, desempenho e memória das texturas | batalha |
