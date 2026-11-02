# Benchmarks de interface

O benchmark de release mede workloads controlados do BGUI. Ele não mede a taxa
de quadros do editor ou do jogo em uma sessão normal.

## Executar localmente

Configure com `-DBENGINE_BUILD_BENCHMARKS=ON`, construa o alvo
`bengine_ui_benchmark` e execute, por exemplo:

```sh
python3 tools/benchmarks/run.py \
  --executable out/build/Release/tools/benchmarks/bengine_ui_benchmark \
  --output benchmark-results \
  --release local \
  --label Linux
```

O executável do benchmark também pode ser chamado diretamente:

```sh
bengine_ui_benchmark --output raw.json --frames 120 --warmup 30
```

O agregador aceita `--frames`, `--warmup`, `--repetitions`, `--scenario`,
`--baseline`, `--regression-percent` e `--fail-on-regression`. Contagens têm
limite de 1 a 10.000. O timeout de cada processo do benchmark é 600 segundos.
Use `--fail-on-regression` junto com baseline compatível.

## Artefatos

O agregador grava JSON bruto por repetição, logs por execução, um relatório
JSON e um Markdown. A gravação final usa arquivo temporário e substituição
atômica. Cada workload precisa fornecer o número esperado de amostras e
atividade não vazia; falhas não devem ser interpretadas como relatório válido.

## Interpretação

Os relatórios registram versões/commits, estado sujo, executável, sistema,
CPU, driver, GPU, resolução e opções de benchmark. Os tempos incluem
`mutation_ms`, `ui_ms`, `submit_ms`, `wait_ms`, `frame_ms` e `gpu_ms`; o tempo
de GPU pode se sobrepor à submissão da CPU e não deve ser somado ao frame time.
Quads contabilizados são comandos antes de batching, não draw calls na GPU.
Widgets fora da área visível permanecem na árvore de UI.

Resultados em renderer de software medem uma carga de CPU/driver e não são
comparáveis a uma GPU física. Compare somente relatórios com ambiente,
configuração, workloads, número de quadros e repetições compatíveis. Os testes
do agregador estão em `tests/benchmark_report_tests.py`.
