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
- **G1e (Vita3K) bloqueada pelo `libshacccg.suprx`.** O app do Vita já traz o backend (`vita_gpu=auto|gles|null` no `storm2.toml`). No modo `auto`, ele usa o renderizador quando existe `ur0:data/external/libshacccg.suprx` e a GPU nula quando não existe. No Vita3K sem o arquivo: [V] cai para a GPU nula e roda como antes. A imagem no Vita3K e no console depende desse arquivo, extraído do console do usuário.

## Etapas
| # | Etapa | Verificação |
|---|---|---|
| G1a | esqueleto no PC: contexto GL, alvos, limpezas, resolves e apresentação | PNGs com as cores de limpeza nos quadros certos |
| G1b | tradutor GLSL | os 66 shaders do censo compilam no driver do PC sem erro |
| G1c | vértices, índices, constantes, viewport, blend/profundidade/stencil/culling | geometria visível nos PNGs |
| G1d | texturas e resolves como textura | logos, tela de título e menu parecidos com as capturas do D3D12 |
| G1e | o mesmo código no Vita (vitaGL) | imagem no Vita3K [precisa do `libshacccg.suprx`] |
| G2 | MSAA, HDR, profundidade como textura, alvos múltiplos, desempenho e memória das texturas | batalha |
