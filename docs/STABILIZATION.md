# Estabilização e validação

Este documento é um guia operacional, não um histórico automático de commits
nem um relatório de testes executados em cada máquina.

## Build e testes

Linux:

```sh
./scripts/build.sh
./scripts/build.sh /tmp/cpp-bengine-build --test
```

Windows:

```powershell
.\scripts\build.ps1
.\scripts\build.ps1 -RunTests
```

No Windows, `build.ps1 -RunTests` executa a suíte CTest completa, incluindo
`MenuBarTests` e `ImageTests` da BGUI. As exclusões desses testes foram
removidas na versão 0.1.3.

Para validar um alvo focado em um build CMake já configurado:

```sh
cmake --build out/build/Debug --target editor_app
ctest --test-dir out/build/Debug --output-on-failure
```

Use `ctest -R <expressão>` para selecionar casos específicos. Informe o comando
exato e o resultado observado; não descreva compilação como teste, nem declare
sucesso para uma execução que não terminou.

## Checklist de mudanças

- Reproduza a falha e identifique os módulos e formatos envolvidos.
- Faça a menor correção completa, sem remover trabalho local não relacionado.
- Adicione regressão para comportamento e para persistência quando aplicável.
- Valide build dos alvos afetados e testes relevantes.
- Revise warnings, recursos GPU e estado de projetos após falhas.
- Atualize a documentação e declare o que ficou sem validação.

## Áreas que pedem validação manual

Renderização e interação dependem de contexto OpenGL, drivers, display, DPI e
entrada nativa. Exportação depende do alvo, bibliotecas e recursos empacotados.
Os testes que pulam por falta de display não contam como confirmação gráfica.

Para uma avaliação de critérios mais ampla, veja
[STABILITY_ASSESSMENT.md](./STABILITY_ASSESSMENT.md).

### Editor workflow and viewport follow-up (2026-11-02)

- The model toolbar's distance input was inheriting a 100-pixel layout minimum;
  explicit bounds and pixel sizing now keep it inside the toolbar. Mode and tool
  buttons have explicit dimensions and spacing. A layout regression checks the
  distance field and adjacent action.
- Camera atmosphere, horizon, fog and directional-light RGB controls reuse the
  existing color picker with undo/redo. The editor camera is also inspectable
  when no scene entity is selected.
- `.editor-workflow.json` adds atomic session storage for workspace/model modes,
  orbital view, selection, inspector section expansion, collider visibility,
  extrusion distance and editor camera configuration/transform. Existing project
  and interface formats remain unchanged. Workspace saving precedes project
  replacement; malformed workflow JSON reports an error.
- `O` toggles normal/orbital navigation over the scene, respecting text focus and
  modal input. Losing the selected orbital target releases its retained pivot.
- Game remains available during playback and respects an explicitly closed panel.
  The runtime publishes transient transform/camera snapshots at up to 30 Hz;
  the editor consumes changed snapshots and restores pre-play values on stop.
  Save, project replacement and exit stop playback before persisting the scene.
  Preview files are removed with the runtime snapshot. Runtime-only entity
  creation/removal, material changes and HUD are not mirrored by this protocol.
- Files actions open from a left click on the file area's background. Existing
  per-file operations reuse the same lifetime-safe popup. Braces also correct a
  dangling `else` introduced by model filtering that omitted folders from the grid.
- BGUI button labels render `...` inside the available width without changing
  their original text; truncation preserves UTF-8 boundaries and expansion
  restores the full label. This required focused changes in the BGUI submodule.
- Validation: 398 CTest cases passed with `BENGINE_RUNTIME_SMOKE=1`, including
  OpenGL rendering, FPP start/pause/resume/stop/restart, preview publication and
  cleanup, workflow round trips, file context actions, model toolbar sizing,
  color undo/redo and label truncation. Interactive appearance and keyboard feel
  still require human review; Windows execution was not exercised on this host.

### Model panel selection and corner square (2026-11-02)

- Root cause: `model_editor_view::update()` had no per-frame caller in the running
  editor. The model surface now drives its panel update through BGUI's existing
  UI lifecycle. Selecting another scene entity loads its model without requiring
  a manual panel update or a separate file selection.
- The panel now has object/source identification, selection modes, a large preview
  and a tools sidebar. Geometry comes from the selected renderer's CPU meshes;
  source polygons are retained only when their vertices and indices match the
  live model. Models without an asset file are supported and can be saved into
  the project's `models` folder without overwriting an existing generated file.
  Saving preserves the selected model's material and instance settings.
- The corner square has a separate cause from the old distance-field layout:
  `ui_scale_system` and `undo_redo_shortcuts_system` inherited the default element
  renderer. Both keyboard-only controllers now emit no draw requirements, even
  if their visual style is marked visible. Keyboard behavior is unchanged.
- Regression coverage checks UI-frame selection changes, in-memory geometry,
  save/reload, component removal, sidebar bounds and both controllers' empty
  draw queues. Manual visual review and Windows execution remain unverified.

### Model transform gizmo (2026-11-03)

- The model preview reuses `editor::transform_gizmo` with an orthographic matrix
  matching its CPU projection. G/R/S manipulate the whole model or selected
  faces/vertices/edges around their centroid. Preview framing remains stable
  during editing, with an explicit frame-model action.
- Picking defers to the gizmo's axis hit test so a transform drag does not also
  change the geometry selection. Input respects modal windows and text fields;
  deleting or switching the source resets the retained gizmo target.
- Saving updates the selected renderer's CPU geometry in place and invalidates
  loaded mesh buffers for their next draw. This preserves materials, instances
  and shader state and avoids loading GPU resources merely to save model data.
- Validation completed: editor/runtime and test targets built successfully;
  `BENGINE_RUNTIME_SMOKE=1 ctest --test-dir out/build/Debug --output-on-failure -j 2`
  passed all 401 tests. This includes an actual projected-X gizmo drag followed
  by save/reload, checking that mesh geometry changes while the scene entity's
  transform stays unchanged. Full interactive visual review remains manual.

### Constant-size gizmo, model navigation and mesh materials (2026-11-03)

- Removed world-unit size clamps that changed gizmo size at extreme zoom. Axis
  endpoints now use a fixed screen length; rotation radii converge without
  world-unit clamps. Invisible axes no longer emit a line to the default screen
  origin. Regression coverage measures the gizmo at zoom factors 1e-6, 1 and 1e6.
- Model navigation reuses `camera_movement_system` with a private preview camera
  and right-button navigation. Normal view looks around from the current eye;
  orbital view moves around the model pivot. The O shortcut and button switch
  modes; workflow persistence stores the model camera mode independently of Scene.
- `Malhas` has one collapsible section per mesh. A shared material editor now
  supplies both Inspector and model controls, with PBR factors, texture maps,
  UV controls, texture previews, drop targets and undo/redo. Bindings validate the
  renderer's identity and lifetime before applying changes or history actions.
- CPU preview projects geometry with the preview camera, clips offscreen wire
  edges and samples the albedo map with perspective-correct UVs. Full PBR lighting
  remains in the engine renderer. Materials are saved with the project, whereas
  `.bmodel` continues to preserve its geometry-only format.
- Validation completed: build succeeded and all 403 CTest tests passed with
  `BENGINE_RUNTIME_SMOKE=1`. New checks cover extreme zoom and near-camera depth
  axes, independent per-mesh material/texture edits with undo, persisted model
  navigation mode, orbital eye movement and normal-view rotation in place.
  Inspector material regressions and the existing rendering/runtime tests also
  passed. Manual visual inspection and Windows execution remain unverified.

### v0.1.3 Windows release validation (2026-11-03)

- Retrieved failing Windows job 125448328382: removed demo fixtures caused missing
  files; Bullet shapes built without RTTI caused dynamic_cast access violations.
  Pending template fixtures and the directly retained convex hull pointer address
  these failures without changing public interfaces.
- BGUI popup measurement ignored wrap-content child minimum constraints (32px
  child in 31px popup). Clamp measured content to the same limits used to arrange
  children; add a platform-independent minimum-size regression.
- WIC maps the unprofiled CMYK red fixture to RGB (228, 50, 43); libjpeg uses
  the direct CMYK conversion. Assert the Windows conversion explicitly while
  preserving strict RGB/grayscale/progressive checks and opaque RGBA checks.
- Remove Windows test exclusions. Local Debug build and all 403 CTest entries
  passed before the final BGUI measurement regression and version bump; final
  local and native Windows results are recorded in the release validation.
- Native Windows CI 41604412601 passed MenuBarTests, ImageTests, collider and
  template regressions; its only failure was the new LinearTest being registered
  with TEST instead of the suite's existing TEST_F. Corrected that fixture use;
  local LinearTests, MenuBarTests and ImageTests all passed afterward.
- Include subsequent pending `.bmat` material persistence, GPU model preview,
  dependency import/export changes and their regression coverage in v0.1.3.
- Release workflow 41611609144 built/tested/packaged Linux successfully but MSVC
  rejected new preview locals named `near` and `far` (Windows macros). Rename
  them to `near_clip` and `far_clip`; no behavior or interface changes.
- Full updated local Debug suite passed all 410 CTest entries with runtime smoke.

## Entrada, layouts e backends da BGUI

| Falha | Correção e cobertura |
| --- | --- |
| Um widget removido em `on_clicked()` causava acesso inválido em `on_pressed()`. | O despacho verifica se a captura foi cancelada antes de chamar o próximo callback. `AuditTests` reproduziu o crash antes da correção. |
| Contagens negativas ou enormes em arquivos de layout provocavam exceção. | O leitor rejeita contagens negativas e só aloca para registros lidos. Os formatos atual e legado continuam sendo aplicados depois que a leitura termina. `AuditTests` cobre arquivos malformados e `DockTests` cobre a restauração de layouts válidos. |
| Um pressionamento seguido de soltura entre quadros desaparecia da interface. | A BGUI despacha as transições do botão esquerdo na ordem recebida e usa a posição de cada evento. `GL3Tests` cobre dois cliques entre quadros e `ComboBoxTests` cobre o fechamento do menu por um clique rápido fora dele. |
| O teste de entrada ignorava a primeira interação em um layout com apenas `overlay`. A geometria da camada de base aparecia sobre retângulos da camada superior. | A entrada percorre as camadas existentes de trás para frente. O renderizador combina retângulos e primitivas na ordem dos widgets, inclusive quando os comandos vêm do cache. A geometria global dos gizmos continua sobre a interface. Cobertura em `AuditTests` e `GL3Tests`. |
| A inicialização sem contexto seguia chamando OpenGL, o desenho alterava o estado do chamador e a janela desconectada ainda encaminhava caracteres. | O backend encerra a inicialização se faltar contexto, restaura o estado alterado pelo desenho e devolve à janela GLFW os callbacks anteriores. `GL3Tests` cobre os três cenários. |

Os testes de backend exigem um contexto OpenGL. Quando o GLFW não consegue
criá-lo, os casos que dependem de janela são pulados. A validação automatizada
em Linux não cobre interação manual no editor nem a execução nativa em Windows.

Na validação local, `ctest --test-dir out -I 1,20 --output-on-failure`
passou as 20 entradas da BGUI, incluindo os seis casos de OpenGL/GLFW com
contexto ativo. Outros 88 casos selecionados do editor passaram, cobrindo
documentos, interação, overlays, renderização e gizmos.

### Encerramento do editor

O Memcheck detectou leituras de memória liberada ao fechar uma cópia de um
projeto. O destrutor de um combo box tentava remover seu menu flutuante
enquanto o layout raiz destruía a coleção de camadas. Na reprodução sem
Valgrind, o processo continuou consumindo CPU após salvar a sessão e a prévia.

O layout agora separa a coleção antes de destruir os widgets. Assim, os
destrutores que consultam o layout encontram uma coleção vazia e válida.
`ComboBoxTest.DestroysOpenPopupWithRootLayout` cobre o caso com um menu aberto.
O caso passou no Memcheck sem erros nem vazamentos. Os 63 casos selecionados
de BGUI, interação e menus do editor passaram no CTest em Linux. O executável
recompilado também fechou a cópia do projeto com código 0 e, sob Memcheck,
zero erros e zero bytes perdidos. O fechamento em Windows e com o jogo em
execução permanece sem validação neste teste.

### Batching e instancing da interface

O backend OpenGL agrupava quadrados consecutivos com materiais idênticos e
expandia cada quadrado para seis vértices na CPU. Agora um buffer fixo define
os cantos e cada instância fornece posição, tamanho, UVs e estilo. Retângulos
sem textura do shader padrão podem compartilhar uma chamada mesmo com cores
e bordas diferentes. Imagens e glifos compartilham chamadas quando o material
e a textura são compatíveis. O lote termina quando muda o recorte ou quando
há geometria de widget entre os quadrados. O caminho de vértices continua
disponível para comandos legados e quando faltam as funções de instancing.

Os testes novos registram as chamadas instanciadas e verificam pixels para
sobreposição translúcida, bordas, recorte, ordem em relação a primitivas,
coordenadas de textura e compatibilidade com comandos legados. O editor e
o runtime compilaram em Linux. As 20 entradas CTest da BGUI passaram com
Mesa limitado a OpenGL 3.3, incluindo os nove casos de `GL3Tests` executados
sem pulos. Também passaram os 29 casos selecionados de renderização e overlays
do editor. O escopo é o backend OpenGL. Medições de tempo por quadro e
validação nativa em Windows permanecem pendentes.

#### UBOs no backend OpenGL

A projeção e os parâmetros compartilhados por lote eram enviados por
uniformes individuais. Agora os shaders usam blocos `std140`. A projeção
é vinculada uma vez por renderização e os buffers de projeção e material
só recebem novos dados quando o conteúdo muda. Os atributos por instância
continuam no buffer de instâncias. O sampler permanece separado, como exige
o GLSL 3.30.

A renderização restaura a ligação genérica de UBO e os buffers e intervalos
que o chamador vinculou aos dois índices usados pela BGUI. Os testes cobrem
essa restauração, a projeção após redimensionamento, as cores de glifos e
a recriação dos buffers ao reabrir o contexto OpenGL.

O editor e o runtime foram recompilados em Linux. Passaram as 20 entradas
CTest da BGUI com Mesa limitado a OpenGL 3.3, incluindo os 10 casos de
`GL3Tests`, e os 31 casos selecionados de renderização, overlays e ciclo de
vida de janela. O tempo de quadro e a execução nativa em Windows não foram
medidos.

### Fachada BGUI para painéis personalizados

A construção de painéis exigia configurar manualmente janela, corpo, rolagem
e grupos de controles. A inspeção de `editor_panel`, `window`, `dock` e
`layout` confirmou que esses recursos já existem na árvore retida; a nova
`bgui::panel_facade` apenas os compõe, sem um segundo gerenciador de painéis.
`bgui::panel_content` oferece linhas, colunas, rótulos, ações, campos e
checkboxes, além de `add<T>()` e acesso aos widgets nativos.

A API existente permanece disponível. O BGUI mantém a propriedade dos widgets;
as fachadas são referências temporárias e não removem conteúdo ao sair do
escopo. Estado de projeto/ECS, validação de valores, histórico e comandos
continuam nos serviços e painéis do editor. Os guias documentam a invalidação
das referências quando a janela/layout é removida e a construção única do
conteúdo, evitando duplicação por frame. Os `AGENTS.md` da engine e do BGUI
foram atualizados para orientar novas extensões e preservar mudanças locais.

Foram adicionados quatro testes `PanelFacadeTest` para registro no dock,
conteúdo fora do cabeçalho, composição/extensão com widgets nativos, callbacks
(incluindo a fila de ações), redimensionamento e permanência dos widgets após
a destruição do builder. A configuração limpa standalone com OpenGL, GLFW e
FreeType desativados compilou `all_tests`, e `PanelFacadeTests` passou. Nessa
configuração, a suíte completa passou 19 das 20 entradas CTest; falhou
`DockTest.TabIndicatorAndSeparatorUseScaledDimensions`, com altura zero da aba
sem fontes. Esse teste não foi alterado por este trabalho.

Na configuração integrada Linux com FreeType, `all_tests`, `editor_app` e
`runtime_app` compilaram, sem avisos nos logs dessa compilação. Passaram as 20
entradas CTest da BGUI selecionadas por
`ctest --test-dir out/external/cpp-bgui -E '^GL3Tests$' --output-on-failure`,
incluindo os quatro novos casos e `DockTests`. Não foram executados os testes
de contexto OpenGL, a suíte completa da engine, interação manual ou validação
nativa em Windows nesta alteração.

### Migração dos painéis, keyframes e retomada de projetos

Os painéis existentes foram migrados para `panel_content` nas superfícies
especializadas, preservando tipos, callbacks, hierarquia e camadas de overlay.
Game e Malhas usam corpos de `panel_facade`; a Hierarquia conserva seu driver
e sua configuração de rolagem. O novo painel Animação usa a fachada e compartilha
as abas inferiores com Arquivos e Debug. A visualização revelou campos simples
com altura de editor multilinha e linhas que reservavam espaço excessivo. O
helper de propriedades agora limita entradas simples a 26 pixels lógicos;
linhas do painel têm dimensões explícitas e a régua tem cobertura de regressão.

A inspeção confirmou que as animações importadas de modelos não forneciam
trilhas para propriedades dos componentes ECS. Foram adicionados o componente
reutilizável `animation`, amostragem linear/constante, adaptadores de canais e
reprodução no runtime. O editor captura/edita/remove chaves, aplica prévia com
histórico e salva as trilhas pela persistência transacional existente. Os
setters preservam recursos associados, sem desserializar/recarregar assets por
frame. A identidade dos alvos é verificada no histórico, e copiar/colar entidades
preserva suas animações. Canais personalizados podem ser expostos pela base de
componentes; ponteiros e mudanças estruturais de assets não são canais animáveis.

A inicialização sem argumentos agora usa o último projeto disponível aberto
com sucesso. A lista de recentes reutiliza a transação de arquivos existente.
Falhas de carregamento permitem recuperar pelo gerenciador principal. O comando
Project Manager da menubar abre essa mesma interface entre execuções do loop da
janela, com opção de voltar ao editor e mantendo a proteção de alterações não
salvas. Nenhum segundo loop nativo é iniciado dentro de um frame ativo.

A configuração limpa da engine foi executada em
`/tmp/bengine-animation-clean`, com editor e runtime habilitados. Os alvos
`editor_app`, `runtime_app`, `animation_tests`, `all_tests`, `editor_menu_tests`,
`editor_persistence_tests` e `editor_interaction_tests` foram compilados em Linux.
As 147 entradas CTest selecionadas desses alvos passaram, incluindo interpolação,
limites, dados inválidos sem mutação, persistência de cenas, entidades removidas
ou substituídas, undo/redo, cópia/cola, propriedades Lua em execução, IDs estáveis
de HUD, fechamento/reabertura do painel, layout compacto e proteção ao abrir o
gerenciador com alterações pendentes.

A validação visual usou um projeto e uma lista de recentes isolados em
`/tmp/bengine-animation-visual`, preservando os projetos do usuário. O editor
abriu esse projeto sem `--project`; a menubar abriu o gerenciador principal e
Voltar ao editor retomou a cena. Na régua foi selecionado 2,5 s e Aplicar prévia
atualizou `position.x` para 1,5 e `rotation.y` para 180 no Inspector e no viewport.
Salvar preservou essas propriedades e as chaves originais. O MCP existente,
por `get_entity`, confirmou o componente e as trilhas salvas; não foi necessário
adicionar comandos de controle remoto. A captura local está em
`out/validation/animation-panel.png`.

Não foi executada a suíte completa da engine, nem validação nativa em Windows.
A lista de canais e os limites da prévia de Play estão em `docs/ANIMATION.md`.

### Animation Clip externo e gravação automática

O primeiro painel armazenava curvas diretamente no componente da cena e exigia
escolher uma propriedade antes de capturar uma chave. O fluxo foi substituído
por componente Animation, arquivo `.banim` reutilizável e detecção automática de
mudanças enquanto Gravar está ativo, seguindo o trabalho com clipes de animação.

`commons::animation_clip` contém duração, Loop e curvas; `commons::animation`
contém referência, Autoplay e velocidade. A persistência carrega os arquivos
antes de substituir a cena e compartilha os dados entre entidades que usam o
mesmo asset. O runtime conserva relógios independentes por componente e não
faz leitura de arquivo por frame. Exportar inclui os clipes e normaliza suas
referências, mantendo o carregamento pelo provedor de recursos embutidos.
Animações antigas continuam legíveis e são externalizadas no próximo save, sem
sobrescrever arquivos existentes.

O Inspector permite adicionar Animation e atribuir clipes. O painel cria,
duplica, atribui, salva e recarrega esses arquivos. O seletor de canais e o
botão de captura manual foram removidos. No modo Gravar, comparar os valores
vivos detecta os canais alterados, cria suas trilhas e atualiza as chaves no
cursor. Uma nova trilha após zero guarda também o valor inicial. Prévia,
undo/redo, troca de seleção e Play não geram chaves espúrias. Mudanças de
Inspector e captura automática no mesmo frame são agrupadas no histórico.

Assets editados são retidos pelo contexto do editor mesmo quando uma entidade
é excluída ou outro clipe é atribuído. Save Project inclui esses assets. Arquivos
alterados externamente não são sobrescritos silenciosamente; Recarregar é uma
operação reversível. O salvamento é recuperável por arquivo, antes da publicação
da cena, sem uma transação global para todos os assets. O guia atualizado está
em `docs/ANIMATION.md`, e as convenções foram atualizadas no `AGENTS.md`.

A configuração limpa em `/tmp/bengine-clip-clean` terminou com editor e runtime
habilitados. Compilaram `editor_app`, `runtime_app`, `animation_tests`,
`game_export_tests`, `editor_menu_tests`, `editor_persistence_tests` e
`editor_interaction_tests`. As 179 entradas CTest selecionadas desses alvos e
da BGUI passaram, incluindo 26 casos de animação e 20 de exportação. A cobertura
nova inclui arquivos externos, clipes compartilhados, curvas fora da cena,
migração legada, gravação automática, agrupamento de undo, reprodução/seleção,
conflitos de disco, reload reversível, assets após exclusão da entidade e
empacotamento de referências.

A validação visual usou `/tmp/bengine-clip-visual`, com projeto e lista de recentes
isolados. Gravar foi ativado no painel em 2 s; mudar Position X de 0 para 3 no
Inspector criou automaticamente `position.x`, com chaves `[0,0]` e `[2,3]`.
Após Save Project, o arquivo `assets/animations/Move.banim` continha as curvas,
e a cena continha somente `clip`, `autoplay` e `speed` no componente. A captura
local foi salva em `out/validation/animation-assets-recording.png`. Não houve
validação nativa em Windows nem execução da suíte completa da engine nesta
alteração.

### Tema escuro neutro e alinhamento do painel de animação (2026-11-07)

A referência visual mostrava campos e botões com pouco contraste e ações do
clipe encostadas no campo de caminho. O tema do editor agora usa preto e cinza
neutros, com roxo para abas ativas, foco de campos e ações principais. Botões
recebem preenchimento e alinhamento central; campos, caixas de seleção e abas
usam bordas e estados de interação consistentes. A linha do tempo acompanha a
mesma paleta. A gravação mantém vermelho como indicação de estado.

No painel de animação, o caminho ocupa uma linha própria, as ações ficam na
linha seguinte e os controles usam altura de 28 px com separação de 6 px.
Rótulos de propriedades usam uma coluna uniforme, com altura explícita para
não herdar os 100 px padrão ao fixar a largura. Mensagens de estado respeitam
a largura do painel. A composição permanece nos widgets e layouts existentes;
nenhum código do submódulo BGUI foi alterado nesta tarefa.

O teste `AnimationTest.ClipPathAndActionsRemainSeparatedAtEditorPanelWidths`
mede a composição com métricas de texto em larguras de 680 e 900 px, verificando
separação vertical, espaço interno dos botões e ausência de sobreposição entre
as três ações do clipe. Os 27 testes `AnimationTest` passaram via CTest em
`out`. Validação em outras escalas de DPI e plataformas permanece pendente.

A inspeção visual em Linux/OpenGL usou uma cópia de projeto em
`/tmp/bengine-interface-preview`, sem salvar alterações no projeto original.
A captura está em `out/validation/editor-dark-purple.png`. Os seletores de modo
do editor de modelos também usam roxo no lugar do azul fixo anterior.

Validação final: a configuração limpa com
`bash scripts/build.sh /tmp/bengine-interface --test` compilou editor e runtime;
após os últimos ajustes, `cmake --build /tmp/bengine-interface --parallel 2`
concluiu com sucesso. Os 28 testes de animação e tema passaram. A repetição da
suíte completa com `ctest --test-dir /tmp/bengine-interface --output-on-failure
-C Release` executou 514 casos: 511 passaram, um foi ignorado
(`RuntimeProcessTest.FppTemplateStartsAndRestartsThroughEditorProcess`) e dois
falharam: `WindowFileDropTest.EngineTextureUploadsDecodedJpegPixels` e
`ModelImportGpuTest.LoadsEmbeddedGlbTextureAndAnimatesThroughExistingRenderer`.
Ambos esperam textura com largura 3 e recebem 2; as mesmas falhas foram
reproduzidas nos executáveis anteriores em `out`, sem reconstruí-los, e estão
fora desta alteração visual. O build limpo também apresentou o aviso existente
em `runtime_process.cpp:231` sobre o retorno de `write` ignorado. Logs locais:
`/tmp/bengine-interface-final-build.log`,
`/tmp/bengine-interface-final-ui-tests.log`,
`/tmp/bengine-interface-final-ctest.log` e
`/tmp/bengine-interface-gpu-baseline.log`.

## Prévia, edição de chaves, rolagem horizontal e API de animação

A régua de animação apenas buscava instantes; agora cada trilha permite arrastar
chaves, mantendo valores, ordenação, proteção contra tempos duplicados e uma
única operação de histórico no soltar. A prévia contínua possui reproduzir,
pausar e parar e agrupa o histórico por sessão sem gravar novas chaves.

No BGUI, `linear::on_update`, `arrange` e o thumb apenas tratavam rolagem vertical.
A extensão mantém o contrato vertical e acrescenta offsets, clipping, entrada
e thumb horizontais, inclusive para layouts horizontais. Shift + roda e eventos
horizontais do touchpad respeitam a prioridade de layouts internos e repassam a
entrada ao pai no limite. A detecção de thumb no fluxo de input existente trata
ambos os eixos. Não foi adicionado outro loop de eventos.

Evidência inicial: os 24 testes `LinearTest.*` e `InteractionTest.*` passaram em
`out/external/cpp-bgui/tests/all_tests`; incluem rolagem horizontal por roda,
touchpad e thumb, redimensionamento e propagação em layouts aninhados.
A validação completa e seus limites estão registrados no fechamento desta seção.

A API Lua acrescenta Animation com comandos validados e aplicados em ordem pelo
`animation_system`. CrossFade interpola canais numéricos e troca canais discretos
na metade, com referência de pose para canais ausentes e transições interrompidas.
Clipes são resolvidos nos comandos, sem recarga por frame; dados transitórios não
entram na serialização. A referência no menu ganhou filtros por categoria/tipo,
com descrições em inglês e português e autocompletar dos controles de animação.

## API Reference: Markdown e remoção reentrante do modal

A referência agora gera títulos Markdown para categorias/tópicos/tipos e blocos
Lua para assinaturas. O renderizador existente do visualizador de documentos foi
extraído para `apps/editor/panels/markdown.hpp` e é compartilhado pelos dois
fluxos, preservando busca, filtros e ownership dos widgets pela árvore BGUI.

Causa confirmada no BGUI: `layout::remove` destruía um elemento dentro de
`vector::erase`. Destrutores de `combo_box` removem seus popups do mesmo vetor de
overlays, alterando tamanho e posições durante o erase externo. O teste
`ComboBoxTest.RemovesModalWithOnePopupAndPreservesFollowingOverlay` falhou antes
da correção: o overlay seguinte foi substituído por um ponteiro nulo. O cenário
com dois filtros isolados passou, portanto o erro específico do alocador reportado
na interface não foi reproduzido literalmente; a corrupção da árvore foi.
A remoção agora move a posse para uma variável local, conclui o erase e só então
destrói o elemento, mantendo a API pública e a remoção dos popups pelos filtros.

Validação parcial: configuração standalone limpa com OpenGL/GLFW/FreeType
desativados e AddressSanitizer/UBSan habilitados; os seis testes `ComboBoxTest.*`
passaram, incluindo os dois novos cenários, sem diagnósticos dos sanitizadores.
Na suíte standalone completa, 18/20 grupos passaram; falharam
`InputAreaTest.TextDrawCallsAreClippedToInputBoxRect` (sem draws de texto) e
`DockTest.TabIndicatorAndSeparatorUseScaledDimensions` (altura de aba zero).
Essa configuração não oferece renderização de fontes; os resultados não validam
aparência nem interação gráfica real. Logs locais: `/tmp/bengine-modal-repro.log`,
`/tmp/bengine-modal-fixed-focused.log`, `/tmp/bengine-modal-fixed-tests.log`.

Validação final: `bash scripts/build.sh /tmp/bengine-animation-clean --test`
compilou editor, runtime e testes em Release. A rodada final executou 539 entradas
CTest: 525 passaram, 13 falharam e uma foi ignorada. `ComboBoxTests` e os quatro
testes de busca/formatação/fechamento da API passaram. As 13 falhas restantes
foram reproduzidas nos executáveis anteriores em `out`, sem reconstruí-los:
InputAreaTests; seleção/hierarquia; duas interações de arquivos; dois testes de
layout/seleção de modelos; gerenciamento de cenas; quatro testes do diálogo de
importação; e dois testes GPU com largura de textura 2 em vez de 3. Não foram
alterados neste trabalho. Comparação e resultado final:
`/tmp/bengine-api-baseline-tests.log`, `/tmp/bengine-api-final-integrated.log`.
O build inicial registrou o aviso existente de retorno de `write` ignorado em
`runtime_process.cpp:231`.

Os dez testes completos de localização/API também passaram em executável ligado
à biblioteca BGUI standalone com ASan/UBSan, sem diagnósticos
(`/tmp/bengine-api-all-asan-tests.log`). O teste antigo de busca foi atualizado
para consultar novamente os parágrafos após refresh, pois a renderização Markdown
substitui esses filhos. Aparência e interação manual no editor não foram
verificadas nesta sessão; o teste automatizado exercitou o botão de fechamento
após abrir e selecionar ambos os filtros.

## Lua: cenas e navegação XZ — 2026-11-07

- `SceneManager` enfileira trocas entre frames, reutilizando o carregador
  transacional sem gravar o manifesto. Snapshots do Play preservam o projeto
  de origem. Testes cobrem cenas inválidas, recursos embutidos e persistência
  das opções do componente `Pathfinder`.
- `Pathfinder` calcula A* XZ limitado, com colisores expandidos pelo raio do
  agente e rota laranja no overlay de colisores/F3. A navegação não controla o
  movimento e não suporta rampas ou navmesh. Ver `lua-api/scenes-and-navigation.md`.
- Configuração limpa em diretório separado concluída; aviso de desenvolvimento
  CMake CMP0135 na obtenção do GoogleTest. Build integrado de editor/runtime
  concluído com `scripts/build.sh out --test`.
- CTest direcionado a `LuaApi`, `ScriptLifecycle`, `LuaCompletion`, `Pathfinder`
  e `SceneManagement`: 50/50 passaram. O teste gráfico
  `RuntimeProcessTest.LuaSwitchesScenesFromSetupAndPlansEnemyRoute`, executado com
  `BENGINE_RUNTIME_SMOKE=1`, passou usando snapshot em pasta diferente da origem.
- Suíte completa: 545 testes, 530 passaram, 2 ignorados e 13 falharam. Permanecem
  falhas em `InputAreaTests`, agrupamento da hierarquia, drop hints, menus de
  arquivos/modelos/cenas, upload JPEG e diálogos/importação de modelos. Não foi
  medida uma linha de base anterior às alterações locais já existentes; estes
  resultados não certificam a suíte inteira. Nenhum segfault na execução final.
- A geometria da rota e a entrega da tecla F3 foram testadas; a aparência final
  da trilha não foi inspecionada manualmente.


### Validação final da prévia, rolagem e blending

`scripts/build.sh out --test` compilou editor e runtime. A execução completa
registrada em `/tmp/bengine-expanded-verified.log` teve 553 testes: 538 passaram,
2 foram ignorados e 13 falharam. Os 35 testes de animação passaram, incluindo
arraste com um único commit, preservação/ordenação de chaves, prévia sem gravação,
comandos Lua, pausa/retomada de blending, canais discretos/ausentes, transições
interrompidas e remoção/substituição de componentes. Os filtros e a referência
renderizada também passaram nos testes direcionados.

O teste de recorte de inputbox foi corrigido para dar geometria ao texto que
pretende verificar: antes, sem geometria de texto, podia passar contando uma
barra ainda não configurada. As barras começam desativadas e só aparecem quando
há overflow real; o teste agora verifica o recorte de glifos efetivos.

Para distinguir regressões da rolagem, foram compiladas cópias dos arquivos
`linear.cpp` e `bgui.cpp` anteriores a esta alteração e religados os executáveis
de teste, sem substituir a árvore de trabalho. Os 12 casos de hierarquia,
arquivos, modelos/cenas, diálogos de importação e upload JPEG que falharam na
suíte também falharam com essa implementação anterior. Logs por caso estão em
`/tmp/bengine-ui-baseline/` e o resumo em
`/tmp/bengine-failed-baseline-results.log`. Essa comparação isola as mudanças de
rolagem/entrada, não constitui uma linha de base de todas as alterações locais
existentes no repositório.

O 13º caso, `SceneWorkflowTest.NativeGraphAndBlockLayoutRendersInsideDock`,
falhou na execução completa com "The library is already initialized"; na
comparação com a implementação anterior houve segfault. Esse cenário de ciclo
de vida da interface não foi corrigido neste trabalho. Os testes de execução
de runtime ignorados dependem do modo smoke gráfico. Não houve inspeção manual
da aparência nem validação de dispositivos touchpad físicos; a entrada dos
dois eixos, Shift e arraste de thumb foi exercitada em testes headless.

Verificação direcionada final: 92/92 casos CTest passaram com o filtro
`AnimationTest|Localization|LuaApi|LuaCompletion|LinearTests|InteractionTests|PanelFacadeTests|InputAreaTests`.
Log: `/tmp/bengine-final-focused-ctest.log`. `git diff --check` passou na engine
e no submódulo BGUI. O aviso existente em `runtime_process.cpp:231` sobre o
retorno de `write` foi observado na primeira reconstrução; nenhuma alteração
nesse arquivo foi feita neste trabalho.

## Gestão de cenas: grafo e Lua visual — 2026-11-07

- Três painéis nativos BGUI no dock de Gestão de cenas: conexões cartesianas,
  scripts do projeto e Lua em blocos. A edição visual reutiliza os buffers e a
  sessão de documentos de Arquivos, com validação de sintaxe sem executar Lua,
  verificação de conflitos no disco e recarga mediante confirmação.
- A leitura/escrita preserva os bytes originais sem edições. Sintaxe que não é
  decomposta com segurança permanece como Lua preservado; expressões dos campos
  continuam usando Lua. Chamadas dinâmicas não são resolvidas pelo grafo.
- Configuração limpa concluída em diretório separado, com dependências já
  disponíveis; apenas o aviso de desenvolvimento CMake CMP0135 do GoogleTest.
  `scripts/build.sh out --test` compilou editor/runtime e executou 553 testes:
  539 passaram, 2 foram ignorados e 12 falharam. As falhas são um subconjunto
  das 13 registradas na validação anterior; nenhuma nova falha foi identificada.
- Oito testes novos de `VisualLua`, `SceneGraphTest` e `SceneWorkflowTest`
  passaram no CTest. A verificação final também cobriu arraste/persistência das
  coordenadas e restauração após fechar/reabrir o grafo.
- O teste OpenGL renderizou os painéis, verificou a seta por leitura de pixel,
  conferiu dimensões dos campos e não encontrou erro OpenGL. A captura da
  interface foi inspecionada visualmente: grade, eixos, nós, seta e blocos
  aninhados visíveis. Não foram alterados arquivos do submódulo BGUI.

### Correção da descoberta de scripts no ciclo nativo — 2026-11-07

Os painéis do fluxo visual dependiam de `panel_manager::update()`, que os testes
chamavam diretamente, mas não era acionado pela aplicação em execução. Assim a
lista de scripts não era preenchida. Cada painel agora instala um elemento
nativo de atualização no próprio conteúdo, seguindo o padrão dos painéis já
existentes, sem interceptar entrada ou desenhar conteúdo adicional.

Os testes de interface passaram a usar apenas `bgui::on_update()`. O caso de
regressão foi reproduzido com a biblioteca anterior: a lista não continha o
script esperado. Com a correção, os oito testes direcionados passaram, incluindo
abertura de scripts, edição/salvamento, reabertura de janelas e renderização
OpenGL. `scripts/build.sh out --test` recompilou o editor e executou os 553 testes:
539 passaram, 2 foram ignorados e as mesmas 12 falhas anteriores permaneceram;
nenhuma falha nova foi identificada.

## Interface compacta e paleta neutra — 2026-11-09

- Abas do dock com padding vertical de 1 px e cabeçalhos de janela com 2 px.
  Propriedades do inspetor usam campos de 22 px, texto em escala 0,27 e menor
  espaçamento entre linhas; seletores, seções e controles auxiliares também
  foram compactados. Os valores são escalados pelo mecanismo existente de DPI.
- Tema e destaques locais do editor, modelos, cenas, animação, Lua visual e
  referência de API substituem os acentos roxos por cinzas. As cores dos dados
  inspecionados continuam sendo os valores dos próprios componentes.
- BGUI: ícones passam a medir a área visível dos glifos, sem reservar o avanço
  e a altura de linha da fonte como se fossem texto. O desenho respeita o
  alinhamento vertical e não recebe o truncamento de rótulos de botões. O
  teste headless com uma fonte sintética reproduziu deslocamentos horizontais
  de 4,4 e 6,6 px nas escalas 1 e 1,5 antes da correção. A barra lateral define
  explicitamente alinhamento central nos dois eixos.
- BGUI: `visual.border.size` e `visual.border.radius` eram consumidos como
  estados de cor pelo prefixo `visual.border.*`. A verificação de neutralidade
  reproduziu um canal vermelho indevido no tema carregado. As dimensões agora
  são lidas antes dos estados de cor, preservando a cor declarada da borda.
  Há regressão específica para tamanho, raio e cor no mesmo bloco de tema.
- Configuração limpa concluída em `/tmp/bengine-compact-ui-build`, com OpenGL,
  GLFW, FreeType e testes habilitados. Os seis grupos CTest `IconTests`,
  `StyleTests`, `DockTests`, `PanelFacadeTests`, `treeTests` e `InputAreaTests`,
  além dos dois casos `EditorThemeTest`, passaram.
  Log: `/tmp/bengine-compact-isolated-focused.log`.
- Os 11 casos `EditorInteractionTest.Inspector*` passaram, incluindo altura
  e espaçamento das linhas, edição e histórico. Os dois casos direcionados
  da barra lateral e aparência nativa também passaram. O teste OpenGL mede
  os quatro glifos com a fonte real, a altura das abas e o texto dos campos
  em escalas globais 1 e 1,5, sem erro OpenGL. Capturas inspecionadas em
  `/tmp/bengine-compact-ui-preview/compact-100.png` e `compact-150.png`.
  Logs: `/tmp/bengine-compact-inspector-tests.log` e
  `/tmp/bengine-compact-native-tests.log`. Essa conferência foi realizada em
  Linux/OpenGL com escala simulada; outros sistemas e monitores físicos
  com DPI diferente não foram verificados.
- `scripts/build.sh /tmp/bengine-compact-ui-build --test` concluiu a compilação
  do editor e runtime. CTest executou 578 entradas: 572 passaram, quatro
  falharam e duas foram ignoradas por dependerem do modo smoke do runtime.
  Log completo: `/tmp/bengine-compact-final-integrated.log`.
  Permanecem falhas em `ModelEditorSelection.UiFramesFollowLiveGeometryAndClearRemovedComponents`
  (navegação orbital), `EditorSceneManagement.CreatesOpensAndDeletesScenesThroughTheWorkspace`
  (busca de ações por tooltip), `WindowFileDropTest.EngineTextureUploadsDecodedJpegPixels`
  e `ModelImportGpuTest.LoadsEmbeddedGlbTextureAndAnimatesThroughExistingRenderer`
  (largura esperada de 3 px, obtida de 2 px com a redução de textura a 70%).
  Não foi estabelecida uma comparação anterior/posterior isolada dessas
  quatro falhas; a suíte geral não está integralmente aprovada.
- A compilação apresentou o aviso já documentado em `runtime_process.cpp:231`
  sobre o retorno de `write`, além do aviso CMake CMP0135 do GoogleTest.
  `git diff --check` passou na engine e no submódulo BGUI. A validação usou
  pasta separada porque outras tarefas também estavam alterando e compilando
  a mesma árvore de trabalho; suas alterações locais foram preservadas.

### Modeling viewport icons and BGUI hover hints (2026-11-09)

- Finding: modeling selection/shading buttons occupied fixed rows outside the
  framebuffer, while geometry/navigation actions used text buttons in the mesh
  sidebar. BGUI had no reusable delayed hover information API.
- Change: modeling tools now use native icon buttons in two viewport overlays,
  anchored to the preview geometry and wrapping at the available width. Extrusion
  distance, mesh/material controls and help remain in the sidebar. Existing edit,
  save, history and navigation callbacks and keyboard shortcuts are preserved.
- BGUI: additive `element::set_tooltip` API with a configurable monotonic delay
  (650 ms by default), a noninteractive native text balloon drawn after widgets,
  UTF-8 wrapping, context bounds, and cancellation through the existing input and
  subtree lifetime paths. See `external/cpp-bgui/docs/TOOLTIPS.md`.
- Regression coverage: delayed/immediate display, leaving/pressing/disabling or
  removing a target, context-edge placement and root replacement; modeling icon
  hit testing, selection callback, overlay bounds, resizing and UI scaling; new
  named icon glyphs. English and Portuguese hints include relevant shortcuts.
- Validation: clean standalone headless BGUI configuration/build succeeded; all
  20 CTest groups passed, including ElementTests and IconTests. Integrated
  build/CTest remains in progress. Interactive GPU appearance has not been
  manually checked.

### FPP geral e leitura hierárquica de Lua

- O catálogo agora contém somente o controlador geral FPP. Projetos FPP novos
  associam `scripts/fpp_controller.lua` ao jogador e persistem referências ao
  jogador e à câmera; arquivos de projetos existentes permanecem intactos.
- O particionamento léxico reconhece guardas compactas e cabeçalhos condicionais
  multilinha, preservando os bytes originais. Comentários e strings não abrem
  blocos. A sintaxe ainda não representável continua como Lua preservado.
- Blocos usam cores por categoria, cavidades para corpos, encaixes e fechamento
  visual. A prévia de arraste mantém oscilação amortecida e indicação de suspensão.
- Cobertura adicionada para estrutura completa do FPP, campos editáveis,
  preservação de CRLF/comentários, remoção e movimentação de corpos compactos,
  abertura do FPP pela UI e referências persistidas no projeto gerado.

### Rig nativo, timeline contínua e dicas detalhadas (2026-11-09)

- Constatação: o céu padrão usava horizonte/zenith azulados; o painel Animação
  tinha controles maiores, não garantia uma superfície escura e reconstruía os
  controles ao navegar. As dicas de modelagem tinham apenas título; o balão não
  habilitava explicitamente o desenho do fundo.
- Câmeras novas passam a usar horizonte e zenith cinza neutro (0,16). Valores
  explícitos de cenas existentes são preservados. Referência Lua, conclusão,
  comando visual, exemplos e templates Empty/FPP acompanham o novo padrão.
- Animação: controles de 22 px, texto reduzido e fundo escuro explícito. Busca,
  reprodução e gravação atualizam widgets retidos; apenas alterações estruturais
  reconstruem as trilhas. A régua permite arrastar o cursor com uma única
  operação de histórico, rolagem horizontal e zoom sem substituir o canvas.
  A prévia não grava novas chaves automaticamente.
- BGUI: extensão aditiva de `set_tooltip` com título e descrição, fontes 0,27/0,24,
  fundo escuro opaco, borda e posicionamento limitado ao contexto. A API anterior
  permanece compatível. Botões de modelagem explicam a ação abaixo do título;
  a dica de câmera também aparece nas sugestões Lua. Teste headless verifica
  os metadados e a geometria do fundo. A propriedade `visual.visible` ausente
  foi a causa do fundo não desenhado.
- Rig: definição e validação em `loaders/model_rig`, persistência nativa em
  `editable_model`, deformação no avaliador CPU `model_pose` existente e canais
  opcionais do Renderer. O painel Malhas cria ossos, ajusta pais/repouso, calcula
  pesos e permite corrigir pesos da seleção. Poses por osso são gravadas pelo
  fluxo automático existente em `.banim`; curvas não entram nos registros de
  cena. A cena guarda somente a pose estática opcional `rig_pose`.
- Identificadores nativos permanecem estáveis ao renomear ossos. Validação rejeita
  ciclos, pais ausentes, pesos inválidos e matrizes de repouso não invertíveis.
  Carregamento de pose inválida preserva o modelo anterior. Clones compartilham
  dados importados imutáveis e mantêm poses independentes. Desfazer a exclusão de
  um osso recupera sua pose; colar pesos de outro rig não deixa referências
  inválidas no modelo. Salvar detecta alterações externas e mantém a edição local.
- Compatibilidade: `.bmodel` sem rig continua versão 1; com rig usa versão 2,
  rejeitada pelas engines antigas. Modelos Assimp expõem poses de nós existentes;
  editar sua hierarquia/pesos é exclusivo do formato nativo. Converter geometria
  importada não converte os clips/rigs originais. Identificadores importados
  dependem da hierarquia; reimportações estruturais exigem revisar as trilhas.
  Reiniciar um clip importado encerra a pose manual na cópia de prévia.

### BGUI invalidation, preview geometry and dock lifetime (2026-11-09)

- Scope: BGUI 1.0 embedded in Bubble Engine 0.1.4. Existing engine and
  submodule working-tree changes were preserved. No public signatures changed.
- Reproduction: the new generic tab removal test crashed with SIGSEGV before
  the fix. `layout::remove` destroyed a window, while `dock::sync_windows`
  dereferenced its retained tab-group pointer. Synchronization now prunes dead
  members and their controls before reading window state, repairs anchor/active
  selection, removes empty groups, and clears stale drop targets. Explicit
  `remove_window` remains supported.
- Preview invalidation: three new tests failed before the fixes. External
  framebuffer assignment previously invalidated style and ancestor layout on
  every call. Identical external frames now do nothing; texture replacement
  invalidates drawing, with layout invalidated only when dimensions change.
  Repeated size/mode setters are also idempotent. Flip changes still invalidate
  cached UVs. Internal texture assignment continues to refresh drawing even
  when the ID and dimensions match, preserving mutable pixel-buffer updates.
- Style propagation: a parent's layout-only change no longer marks descendant
  styles dirty. Visual inheritance and existing global theme/scale invalidation
  remain intact. This avoids needless descendant style work during size changes.
- Deterministic performance regression coverage: 1,000 visible widget rows,
  ten unchanged draws with no additional row geometry construction, then a
  single changed row with only that row rebuilt. These are operation counts,
  not wall-clock/FPS guarantees or a benchmark of 1,000 ECS entities. Additional
  tests cover repeated external frames, framebuffer replacement, UV flipping,
  natural/explicit dimensions, and repeated dock resize at scales 1 and 1.5
  with committed preview geometry matching draw commands in the same frame.
- Changed implementation: BGUI `elem/image.hpp`, `elem/image.cpp`,
  `elem/element.cpp`, `lay/dock.cpp`; regression coverage in `image_tests.cpp`,
  `element_tests.cpp`, `dock_tests.cpp`. This section records only this task's
  additions; other local modifications have separate stabilization entries.
- Standalone validation: clean configuration in
  `/tmp/bgui-stability-20261109`, headless null backend, C++20, FreeType enabled;
  build succeeded and all 20 CTest groups passed (2.85 s). Logs:
  `/tmp/bgui-stability-last-build.log`,
  `/tmp/bgui-stability-final-ctest.log`. The initial FreeType-disabled run
  passed 19/20 groups; the existing scaled tab separator test reported zero
  text height. Repeating with FreeType enabled passed all 24 docking tests.
  Before-fix evidence: `/tmp/bgui-stability-before.log` and
  `/tmp/bgui-stability-dock-before.log`.
- Integrated focused validation: all 15 CTest cases matching
  `DeferredViewportResize|EditorViewport\.|Gizmo|CameraRelease` passed using
  the Linux/OpenGL display available to the test process (1.36 s). This includes
  deferred framebuffer resize, empty layout, camera release/recreation, gizmo
  capture outside the viewport, world rotation, screen size and direction
  overlays. Log: `/tmp/bgui-stability-viewport.log`. No manual visual inspection,
  physical DPI/multi-monitor checks or other operating systems were exercised.
- Deferred dock selection audit (2026-11-10): ordinary tests did not expose a
  second lifetime defect, but Valgrind reported 38 invalid memory accesses in
  36 contexts when a queued tab selection ran after generic window removal
  or root replacement. Dock tabs now cancel their queued selection when the
  tab dies; surviving callbacks synchronize window membership before selecting.
  The lifetime token is allocated once per dock tab, with no per-frame token
  allocation. Three removal/root replacement cases then passed under Valgrind
  with zero errors and zero live heap blocks at exit. Logs:
  `/tmp/bgui-stability-queued-valgrind-before.log`,
  `/tmp/bgui-stability-lifetime-valgrind.log`,
  `/tmp/bgui-stability-lifetime-valgrind-tests.log`.
- Final standalone rebuild after the deferred-selection fix succeeded. All 20
  CTest groups passed again, including 26 docking tests (4.36 s total).
  Logs: `/tmp/bgui-stability-lifetime-build.log` and
  `/tmp/bgui-stability-lifetime-ctest.log`.

### Pre-publication validation (2026-11-13)

- `bash scripts/build.sh out --test` configured and built the editor, runtime
  and test targets successfully. CTest executed 604 entries: 545 passed,
  13 failed and 46 were skipped. The full suite is not passing.
- Failures cover FPP project/script creation and visual representation, script
  attachment through the UI, modeling selection/extrusion, scene management,
  the neutral-camera Lua test and the block-editor script context menu.
  This check covers the existing working-tree changes; it does not
  establish whether those failures were present before them.
- Graphics-dependent tests were skipped because no display was available;
  runtime smoke tests also require their optional execution mode. No manual
  graphics or other-platform verification was performed in this session.
- `git diff --check` passed in both cpp-bengine and cpp-bgui. Complete build
  and CTest output: `/tmp/bengine-precommit-validation.log`.

## Editor workflow and text-input changes (2026-11-13)

Input inspection found missing GLFW Page Up/Down/keypad mappings and inputbox selection reset without a drag-selection lifecycle. Added keypad navigation aliases only for focused text fields, page movement/Shift selection, mouse drag selection, and vertical cursor reveal for scrolling multiline input. Native numpad events remain available to viewport navigation. Backend changes live in the nested `cpp-bgui/backend` submodule.

Added regression coverage in `TextEditor.PageNavigationExtendsSelectionAndRespectsDocumentLimits` and `TextEditor.MouseDragSelectsTextForReplacement`. Prefab persistence and shader round-trip/failure behavior have separate regression tests. Validation results are recorded after the integrated build. Physical keyboard Num Lock combinations, IME behavior and interactive docking/weight-paint usability require desktop verification.

## Creative MCP authoring and runtime capture — 2026-11-13

- Extended the existing stdio server with project creation/selection, asset discovery,
  native mesh creation, validated scene composition, Lua syntax checking, `.banim`
  authoring and guarded startup-scene edits. Existing inspection tools remain available.
- Reused project templates, native validators and recoverable transactions. Writes
  require an exact preimage for existing files and reject traversal/symlinks. Active
  tests block edits. The shared runtime launcher has an opt-in project-preserving
  path; its previous editor snapshot ownership behavior is unchanged.
- Added bounded runtime tests, camera PNG capture, persistent specifications/reports,
  stderr/stdout isolation and MCP image content. Runtime initialization errors now
  return a logged error instead of escaping `main`. Capture is camera-only, excludes
  HUD, uses real simulation time and requires agent review of visual expectations.
- Clean configuration/build used `/tmp/bengine-mcp-build`, with cached GoogleTest
  and miniaudio sources because network DNS prevented the integrated script's first
  dependency fetch. Built `bengine_mcp`, `runtime_app`, `runtime_process_tests`.
- Validation: 12 stdio MCP regressions passed; six runtime process lifecycle tests
  passed. The protocol test uses a fixture PNG to verify image transport, not rendering.
  Two existing graphics tests and the opt-in MCP capture test were skipped in CTest.
  An explicit real-runtime capture attempt could not open display `:0`. Verified the
  resulting MCP status contains exit code 1 and `Failed to initialize GLFW`, with no
  image. Actual rendered PNG appearance/resolution and Windows execution remain
  unverified in this environment. No claim of a fully executed graphical suite.
- Run the real capture workflow with `BENGINE_MCP_GRAPHICS_TEST=1 ctest --test-dir
  <build-dir> --output-on-failure -R '^mcp_runtime_capture$'` in a working OpenGL session.

### Portable release packaging (2026-11-13)

- Added a self-contained portable entry point with a ZIP payload, persistent user
  state, bundled Linux file picker and native precompiled folder-export template.
- Release packaging rejects development builds and includes Windows MSVC runtime
  DLLs. CI verifies payload hashes/Tcl/template loading without PATH tools and
  checks Linux editor first-frame readiness from the extracted portable ZIP.
- Local Python release tests: 15 passed; native file dialog tests: 6 passed;
  focused release/export/build/dialog CTest run: 32 passed. The affected editor target compiled.
  Full portable generation could not run locally: PyInstaller was absent and pip
  could not resolve the package server in the restricted environment. Windows and
  the frozen executable still require the release CI and desktop validation.

## Dock controls and large text rendering (2026-11-13)

- Confirmed that dock overflow controls were passive text and tab selection always forced the panel open. The overflow is now a button with the existing BGUI context menu (expand/collapse, next tab, float panel). Clicking the active tab toggles its content; selecting another tab or focusing a window expands it. Collapsed outer areas return space to the center without changing saved ratios. Collapse state is transient; workspace serialization remains unchanged.
- Separated line numbers from scope guides with `text::set_line_numbers_enabled` and the inputbox forwarding method. File editors enable numbers and leave scope guides disabled.
- Text caches UTF-8 decoding and layout metrics, indexes rendering in 64-line chunks, and starts drawing at the chunk intersecting the clip rectangle. Unwrapped text without scope guides stops drawing below the viewport. The original byte buffer, global selection/caret offsets, syntax colors, and save path remain intact. Wrapped text and scope-guide rendering retain the full traversal.
- This is rendering chunking, not disk paging: the document, highlight state and undo snapshots remain in memory; edits still rebuild decoding/highlighting. Extremely long individual lines and full-document editing costs are not eliminated.
- Regression coverage exercises tab toggling, opening the overflow menu, compacting/restoring a bottom dock, and clipped chunk rendering against the full traversal with line numbers. Validation results are recorded below after execution.

### Dock presentation and hierarchy virtualization (2026-11-14)

- Side docks collapse to a narrow rail of square tabs with centered dots;
  top and bottom docks give their freed height to the center or opposite dock.
  The three-dot menu uses explicit circle widgets and clamps to the viewport.
- Hierarchy scenes with at least 128 entities reuse visible tree rows and
  spacers for scrolling. Expansion, filtering, selection and drag targets bind
  to the current entity; a periodic snapshot also catches changes without an
  editor event. Existing text chunking and independent line numbers remain in
  the maintained BGUI submodule.
- Added headless dock regressions for both lateral areas, both vertical areas,
  center and opposite neighbors, scales 1 and 1.5, and menu placement. Added
  an editor test for a 1,000-entity hierarchy with scroll and filter rebinding.
  Standalone BGUI configured with OpenGL, GLFW and FreeType disabled, built
  successfully and passed all 20 CTest groups. Visual display and GPU behavior
  were not manually verified.

- Follow-up: the three-dot popup was enabled but its parent control sorted
  behind pinned windows, so the window painted over it and received clicks.
  The overflow control now sorts above windows. A regression sends real mouse
  press/release events to the popup option and checks that it collapses the
  panel. Collapsed side rails use the normal tab header height for their square
  width and each tab height, including groups with multiple tabs. Sole outer
  docks also shrink their allocated width or height when collapsed. The BGUI
  headless build and all 20 CTest groups passed after these changes. The
  integrated editor_interaction_tests target compiled and its 1,000-entity
  hierarchy case passed; the full interaction binary passed 55 of 56 tests.
  KeypadViewsAndProjectionDoNotRepeatWhileHeld failed on camera rotation and
  orthographic toggling, separate from dock rendering. Desktop rendering remains
  to be checked interactively.
