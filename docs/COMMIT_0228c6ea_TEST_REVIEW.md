# Auditoria dos testes de 0228c6ea

## Escopo e evidência

Revisão analisada: `0228c6ea4c329f74a3347163ac0c1099cd77a52d`, de 13/11/2026.
O repositório local já contém dois commits posteriores; resultados atuais não são a linha de base histórica.

O [job Linux da revisão](https://github.com/D4nielStone/cpp-bengine/actions/runs/43369590298/job/131701987442)
confirma 604 entradas: 545 aprovadas, 13 falhas e 46 ignoradas. A lista abaixo vem desse job, não da contagem de uma execução posterior.
O [job Linux do pai 33b0d687](https://github.com/D4nielStone/cpp-bengine/actions/runs/42448042526/job/128631062805)
falhou na compilação de `api_reference.hpp:164` por uma referência não constante a um temporário `panel_content`.
Portanto não existe nesse CI uma comparação executável completa pai/filho: não se pode declarar que os 13 representam 13 regressões independentes do produto.
A atribuição abaixo combina o diff e as mensagens dos testes; distingue defeito de produção, expectativa incompatível e falha da preparação do teste.

## Falhas por sistema e correções prioritárias

| ID CTest histórico | Teste | Diagnóstico | Prioridade e arquivos |
| --- | --- | --- | --- |
| 65 | `ProjectCreationTest.CreatesAnFppProjectTemplateWithAPlayableScene` | `player` e `camera` persistidos como zero, esperados 3 e 4. O script gerado está vazio, sem declarações de propriedades. | P1: `apps/editor/systems/script_templates.hpp`; verificar `project_creation_system.cpp` e `tests/editor_persistence_tests.cpp`. |
| 120 | `EditorScriptTest.CreatesTemplateWithoutOverwritingOrEscapingFolder` | O script criado não contém `function update()`: o preset padrão FPP resolve a entrada em branco. | P1: mesmo seletor; `tests/editor_script_tests.cpp`. Manter verificações de conflito e escape de diretório. |
| 121 | `EditorScriptTest.OffersOnlyTheGeneralFppTemplate` | Espera uma opção, mas o catálogo contém duas, incluindo arquivo em branco. Expectativa incompatível com o catálogo, não um segundo defeito de execução. | P2: alinhar `tests/editor_script_tests.cpp` às opções suportadas e conferir o conteúdo de cada preset. |
| 122 | `EditorScriptTest.FppControllerUsesSelectedCameraYawAndMovesTheSelectedPlayer` | `set_property("player", ...)` retorna falso porque não existe a declaração no arquivo vazio. | P1: seletor; manter teste funcional de câmera, movimento, remoção e persistência. |
| 132 | `EditorAssetWorkflowTest.CreatesScriptAndUpdatesDirtyAndSaveThroughTheUiLoop` | Selecionar FPP pela interface gera arquivo sem `Input.GetAxis`. | P1: alinhar enum e índices usados em `apps/editor/panels/files_view.hpp`; preservar o teste em `tests/editor_asset_workflow_tests.cpp`. |
| 285 | `LuaApi.BuiltinFppTemplateScriptsMoveLookAndCollectUsingTheNewApi` | Falha ao definir `player`, pela mesma seleção incorreta. Não é evidência de quebra da ligação Lua. | P1: seletor e `tests/lua_api_tests.cpp`; verificar também o FPP gerado em runtime. |
| 588 | `VisualLua.GeneralFppHasStructuredEventsConditionsAndEditableMovement` | Zero funções, condições e atribuições: a entrada do parser é a string vazia. | P1: corrigir preset antes de alterar o parser de `apps/editor/services/visual_lua.cpp`; preservar `tests/visual_script_tests.cpp`. |
| 599 | `SceneWorkflowTest.GeneralFppOpensAsNestedBlocksWithoutOpaqueScript` | Sem corpos, evento update ou campos de condição, pois o arquivo aberto está vazio. | P1: seletor; manter cobertura de estrutura e preservação de bytes em `tests/scene_workflow_tests.cpp`. |
| 306 | `LuaApi.NewCamerasExposeNeutralGrayWithoutOverridingSavedColors` | Aborta com `attempt to index a nil value`: `setUpApi` consulta `bubble.input`, mas o novo teste não cria a tabela. | P1 para confiabilidade da suíte: corrigir preparação em `tests/lua_api_tests.cpp`, seguindo os outros testes. Não modificar o contrato público de Lua para mascarar a preparação incompleta. |
| 264 | `ModelEditorSelection.UiFramesFollowLiveGeometryAndClearRemovedComponents` | Atalho orbital e arraste não alcançam o viewport. O ponto fixo `image + 20` pode atingir os novos botões sobrepostos. A falha já aparece na validação intermediária documentada de 09/11. | P2: `tests/editor_menu_tests.cpp`, com ponto livre e confirmação de ownership; conferir `apps/editor/panels/model_editor_view.hpp`. O commit posterior a6865c27 já desloca o ponto para o centro. |
| 269 | `EditorSceneManagement.CreatesOpensAndDeletesScenesThroughTheWorkspace` | O diff trocou a busca de texto por tooltip, mas `scenes_view.hpp` ainda cria botões de texto. O log mostra `[Create scene]` enquanto o teste busca a dica de `Criar cena`. | P2: restaurar busca pelo texto traduzido em `tests/editor_menu_tests.cpp`; não converter a interface para satisfazer um seletor incorreto. Conservar abertura transacional, cena inicial e confirmação de exclusão. |
| 276 | `ModelEditorTools.DragsExtrusionCutsInEachModeAndExtractsMeshSelection` | A falha efetiva está na extração: não encontra `quads_parts/quads-malha-1.bmodel`. O log não atribui o defeito à extrusão. Seleção e nome sugerido precisam ser conferidos após os updates nativos. | P2: `tests/editor_menu_tests.cpp`; inspecionar `model_editor_view.hpp::show_extract` e `systems/model_mesh_extract.hpp`. a6865c27 já acrescenta updates entre seleções e confere o nome do modal. Preservar teste de conteúdo exportado e histórico. |
| 600 | `SceneWorkflowTest.RightClickInBlocksCreatesAppliesAndRemovesScripts` | Sem alvo de mouse e sem popup `Aplicar a objeto...`. O log registra campo em `(841,595)` com `15x30`; o teste não confirma visibilidade/recorte nem janela ativa. | P2: preparar área visível e foco em `tests/scene_workflow_tests.cpp`; conferir `workflow_panel::context_input` em `apps/editor/panels/scene_workflow.cpp`. A reprodução mediu `tree` terminando em x=784 e o campo começando em x=841: ele estava fora do recorte. Com área suficiente e confirmação do alvo, o fluxo completo passou na verificação direcionada. Manter guards de Play, identidade, undo e exclusão confirmada. |

## Causas e atribuição

O defeito de produção comprovado é a divergência adicionada neste commit em `script_templates.hpp`:
`enum class lua_script_preset { fpp_controller }` vale zero, enquanto a lista coloca `Arquivo em branco` em zero e o FPP em um.
Sete falhas são consequências desse defeito; a oitava do mesmo grupo é a expectativa de que só exista uma opção.
A correção localizada é representar ambas as entradas no enum e testar a associação de cada uma ao conteúdo correto.
Isso preserva arquivos de projetos existentes e não exige alterar API Lua, parser, formato de cena ou arquitetura.

A falha de câmera é um teste novo com preparação incompleta; a busca de cenas é uma regressão do próprio teste demonstrada pelo diff.
Os dois casos de modelagem e o menu de blocos exigem distinguir entrada sintética inválida de defeitos reais do editor.
O registro de 09/11 em `docs/STABILIZATION.md` já cita modelagem orbital e cenas, mas pertence ao desenvolvimento acumulado do próprio commit;
não é prova de que esses casos falhavam no seu pai. As 46 entradas ignoradas tampouco certificam renderização, lifetime OpenGL ou smoke do runtime.
Nenhuma das 13 falhas identifica diretamente deformação de rig, normalização de pesos ou gravação de poses; o cenário gráfico de rig foi ignorado.

## Ordem de estabilização da v0.1.5

1. P0: corrigir bloqueios de compilação e distribuição: includes de rigs, construção do contexto ECS, colisões do SDK Windows, `/bigobj`, toolchain de watchdog e versão CMake/tag. Não substituir esses bloqueios por exclusões de testes.
2. P1: corrigir a associação de templates e executar os sete testes dependentes, persistência do projeto e smoke do FPP. Corrigir a preparação do teste Lua que aborta.
3. P2: alinhar os seletores e a geometria dos testes de interface, verificando os alvos reais antes dos eventos. Manter expectativas de persistência, undo e arquivos exportados.
4. Executar build integrado, CTest Linux/Windows e smoke gráfico dos pacotes finais. Somente então mover a tag ainda sem release publicada e publicar os ZIPs com checksums.

A publicação e os resultados da validação da correção são registrados separadamente; esta lista descreve o commit histórico, sem reclassificar seus resultados com executáveis atuais.
