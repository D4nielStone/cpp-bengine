# Vila Aurora — Bubble Engine

Abra `project.bengine` no Project Manager da Bubble Engine e inicie o runtime.

## Controles
- WASD: andar em relacao a camera.
- Shift esquerdo: correr.
- Espaco: pular.
- Mouse: orbitar a camera.
- Esc: liberar cursor e pausar movimento.
- Enter: recapturar cursor.

Cidade low-poly original com 16 casas, ruas, calcadas, praca com fonte, bancos, arvores, postes e carros decorativos. O personagem possui colisao capsula e animacao procedural dos membros. A escultura da praca usa uma animacao .banim reutilizavel.

A camera evita as caixas das casas; os limites de oclusao estao no script e precisam ser atualizados se voce mover as casas. Casas e carros sao cenografia, sem interiores nem trafego.

Todos os modelos sao nativos .bmodel, gerados para este projeto, sem texturas externas. O script completo esta em scripts/terceira_pessoa.lua. Criacao e validacao de assets realizadas via MCP stdio; CRIACAO_MCP.json registra as chamadas.

Resultado da tentativa de runtime: {"running": false, "timedOut": false, "exitCode": 1, "log": "[runtime] Failed to initialize GLFW.\n"}

Validacao adicional sem janela: todos os modelos e a animacao carregaram nos loaders nativos; o script executou com os bindings reais da engine, com entradas simuladas para andar, correr, pular, liberar/recapturar cursor e 120 atualizacoes sem erro. Colisao em gameplay e aparencia grafica ainda requerem teste no runtime com display.

Correcao de compatibilidade: o ID 1 foi reservado para a camera do editor. O terreno agora usa o ID 284.
