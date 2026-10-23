# ReXGlue codegen register-model benchmark (PS Vita)

Compara, no Cortex-A9 do Vita, o mesmo código PPC recompilado com 5 conjuntos de opções do codegen:

| Variante | Opções |
|---|---|
| v0 | padrão (todos os registros no `PPCContext`), como o build atual do Storm 2 |
| v1 | `ctr/xer/cr/reserved` como variáveis locais |
| v2 | v1 + `r0/r2/r11/r12/f0/v32-63` como locais |
| v3 | v2 + não voláteis (`r14-31`, `f14-31`, `v14-31`, `v64-127`) como locais |
| v4 | v3 + `skip_lr` + `skip_msr` |

Casos (`rexglue/tests/ppc/asm/bench.s`, gerado por `vita/ppc_tests/tools/gen_bench.py` a partir de um modelo Python independente):
- `int`, `fp`, `fma`: laços sintéticos originais;
- `struct`, `vec3`: laços com o formato de código de jogo compilado (registros não voláteis, quadro de pilha, chamadas, `cr6`, `fmadds`).

O programa confere todos os casos de todas as variantes contra os valores esperados e depois mede. As variantes são intercaladas por caso, em 3 rodadas, e fica o melhor tempo de cada uma.

## Gerar e compilar
```
py -3 -I vita/ppc_tests/tools/gen_bench.py rexglue/tests/ppc/asm/bench.s
(build do rexglue: ppc_tests gera tests/ppc/bin/bench.bin; cmake --install)
py -3 -I vita/bench/tools/make_variants.py rexglue/out/install/win-amd64/bin/rexglue.exe rexglue/out/build/win-amd64/tests/ppc/bin rexglue/tests/ppc/asm vita/bench/gen
cmake -G Ninja -S vita/bench -B vita/bench/build-vita -DCMAKE_TOOLCHAIN_FILE=%VITASDK%/share/vita.toolchain.cmake
cmake --build vita/bench/build-vita     (-> rexglue_bench.vpk, TITLE ID RXGT00002)
```
No PC, para validar: `-B vita/bench/build-host -DCMAKE_CXX_COMPILER=clang++`.

## Rodar no Vita
Instalar `dist/rexglue_bench.vpk` com o VitaShell e abrir "ReXGlue Bench". A tela fica amarela enquanto roda, com uma barra de progresso embaixo, e depois verde (tudo certo) ou vermelha (algum caso errado). Apertar qualquer botão para sair. O resultado fica em `ux0:data/storm2port/bench.log`.
