# Cobertura dos recursos essenciais

Os testes são registrados pelo GoogleTest no CTest. O fluxo da CI está em
`.github/workflows/build_and_test.yml` e usa `bash scripts/build.sh out --test`
no Linux e `scripts/build.ps1 -BuildDirectory out -RunTests` no Windows.

| Base | Cobertura principal |
|---|---|
| ECS, entidades e componentes | `commons_tests.cpp`, `engine_foundations_tests.cpp`: máscaras, entidades vivas, IDs únicos, reposição/remoção de componentes, lookup e iteração |
| Transform e hierarquia | `engine_foundations_tests.cpp`: composição TRS, posição mundial/local, reparent, ciclos, pai singular e serialização; gizmos em `editor_interaction_tests.cpp` |
| Física Bullet | `engine_foundations_tests.cpp`: seis formas suportadas, propriedades/validação, sincronização do mundo, forças, travas, raycast, queda e contato; persistência em `editor_persistence_tests.cpp` |
| Scripts Lua e template FPP | `lua_api_tests.cpp`, `editor_script_tests.cpp`, `runtime_process_tests.cpp`: APIs, lookup, propriedades, execução, erros, logs, múltiplos scripts e inicialização de um projeto criado pelo template |
| Projetos e cenas | `editor_persistence_tests.cpp`, `editor_persistence_transaction_tests.cpp`, `persistence_integrity_tests.cpp`: round-trip, caminhos, recuperação/rollback, truncamento em cada prefixo, corrupção de esquema/hierarquia/journal, preservação da cena ativa e proteção de links |
| Memória durante persistência | `persistence_integrity_tests.cpp`: liberação de Transform/Lua/Bullet substituídos, referências em falhas repetidas, ownership externo e encerramento explícito do ECS. Não comprova ausência global de leaks. |
| Câmera, materiais, luzes e renderização | `editor_render_tests.cpp`, `editor_interaction_tests.cpp`, `lua_api_tests.cpp`: framebuffer, resize, PBR, sombras e orientação hierárquica |
| Editor, BGUI e assets | `editor_*_tests.cpp`, `text_editor_tests.cpp`, `lua_completion_tests.cpp`, `ui_scale_tests.cpp`, `window_file_drop_tests.cpp`, testes da BGUI: documentos, menus, histórico, Inspector, docking, importação e entradas |
| Exportação e inicialização | `game_export_tests.cpp`, `executable_archive_tests.cpp` e `export_runtime_smoke.py`: estrutura de pacote, arquivo único, projeto mínimo gerado e inicialização longe do diretório de trabalho |

## Executar somente as bases sem janela

Depois de configurar com `BUILD_TESTING=ON`:

```sh
cmake --build out --target engine_foundations_tests --parallel 2
./out/tests/engine_foundations_tests
```

Esse executável usa a biblioteca real da engine e Bullet, mas não inicializa GLFW
nem cria contexto OpenGL. Seus passos de simulação são fixos, sem sleeps. Isso
permite verificar gravidade, colisões e sincronização sem depender do relógio ou
da GPU. As dependências de compilação da engine continuam necessárias.

A suíte completa inclui testes gráficos que podem fazer skip quando não há
contexto disponível. Um CTest aprovado, sozinho, não comprova que todos esses
ramos executaram: confira também a saída detalhada dos testes gráficos.

Esta matriz descreve contratos exercitados, não uma afirmação de cobertura de
100% das linhas ou de todas as combinações possíveis. Distribuição relocável,
plataformas/drivers distintos e uso interativo prolongado exigem validação própria.
