# Painéis do editor

Os painéis do editor implementam `editor::ui::editor_panel` e fornecem
`build(bgui::dock&)` e `update()`. `panel_manager` possui os painéis
registrados, cria seus widgets no dock e encaminha atualizações. Os widgets
continuam pertencendo às árvores de elementos do BGUI.

## Painéis principais

- **Project Manager**: oferece `Empty Project` e `FPP - First Person`. O
  template FPP cria uma cena inicial com jogador cápsula, câmera, física e
  scripts Lua de movimento e controle de câmera; os arquivos gerados ficam
  dentro do novo projeto.
- **Scene**: viewport de edição, seleção, gizmos, grade e navegação da câmera
  do editor.
- **Game**: prévia da primeira câmera de cena, seguindo a escolha do
  `runtime_app`; usa a câmera do editor como alternativa quando não há câmera
  de cena. O framebuffer acompanha o tamanho do painel.
- **Hierarchy**: lista e seleção de entidades.
- **Inspector**: transforma, câmera, iluminação e componentes suportados.
- **Arquivos**: arquivos do projeto, documentos Lua/texto e importação de
  assets.
- **Edição do modelo**: edição topológica do modelo 3D da entidade selecionada
  no modo Modelagem.
- **Debug**: log de scripts Lua.
- **Editor Diagnostics**: métricas locais de frame e renderização.

O painel **Game** é ocultado enquanto o runtime externo está executando. O
runtime roda em outro processo, portanto a prévia não mostra a simulação desse
processo; ela renderiza o ECS de edição. O runtime atual não fornece sistema de
HUD de jogo. O painel indica essa ausência em seu status.

## Espaços de trabalho

A barra lateral centraliza os modos **Editor**, **Modelagem** e **Gestão de
cenas**. Cada botão ativa uma dock própria. O modo **Modelagem** contém um
único painel para editar a geometria do modelo 3D da entidade selecionada; o
Files View permanece no modo Editor. O painel acompanha a seleção na cena e na
Hierarquia e grava as alterações em um arquivo `.bmodel`, atualizando o modelo
da entidade na cena.

Para trocar o modo por código, use
`editor_ui::set_workspace_mode(editor_workspace_mode::...)`.

## Persistência e navegação

O layout do dock e as sessões de documentos são guardados por projeto por meio
de `editor_workspace`. A interface BGUI é responsável por widgets/layout; o
`editor_context` e comandos do editor mantêm estado e ações.

Para adicionar painel, prefira `editor_ui::add_panel<T>(...)`. Não faça o painel
manter referências de componentes ECS sem considerar remoção/substituição;
widgets ligados a componentes devem usar ownership/callbacks e validar o
ciclo de vida apropriado.

Convenções de layout, helpers de campos e integração com histórico devem seguir
os padrões de `apps/editor/core/panel.hpp` e dos painéis existentes.

O atalho **O**, com o mouse sobre a cena e sem um campo de texto em edição,
seleciona a visão normal/orbital. Clique com o botão esquerdo no espaço livre da
área de arquivos para abrir as ações da pasta. Sem uma entidade selecionada, o
Inspetor permite configurar a câmera do editor, incluindo céu e neblina.

A sessão do projeto também guarda modos, seleção, seções do Inspetor e a câmera
em `.editor-workflow.json`. Durante Play, Scene e Game acompanham transformações
e câmeras das entidades existentes; parar restaura o estado anterior. Salvar,
trocar de projeto ou sair encerra Play antes de persistir a cena. Entidades
criadas/removidas apenas no runtime e alterações de materiais/HUD ainda não são
espelhadas nessa prévia.

O painel **Edição do modelo** acompanha o objeto selecionado na cena/Hierarquia.
O cabeçalho identifica o objeto e a origem; a coluna à direita concentra extrusão,
Loop Cut e salvamento. Modelos sem arquivo de origem usam a geometria em memória
e podem ser salvos na pasta `models` do projeto. Alterações pendentes continuam
protegidas: salve o modelo atual antes de trocar o objeto em edição.

O gizmo do painel de modelo reutiliza os controles **G** (mover), **R** (girar)
e **S** (escalar). Arraste um eixo para transformar o modelo inteiro ou a seleção
de faces, vértices ou arestas. **Enquadrar modelo** ajusta novamente a visualização.
A geometria editada é aplicada ao objeto ao salvar, preservando seus materiais.

A câmera do painel de modelo tem visões **Normal** e **Orbital**, alternadas por
**O** ou pelo botão de visão. O botão direito olha/orbita; scroll aproxima/afasta;
Ctrl + Shift + arraste com o botão direito desloca a vista. A navegação usa os
mesmos controles da cena e não disputa o arraste esquerdo com o gizmo.

O painel **Malhas** contém uma seção recolhível por mesh. Cada seção controla cor,
metallic, roughness, AO, mapas de albedo/normal/PBR, repetição e deslocamento de UV,
e WorldTexture. Os campos de textura aceitam caminhos ou arquivos arrastados.
Os controles **Material .bmat** carregam um material por caminho ou arquivo
arrastado; **Salvar .bmat** cria um arquivo reutilizável em `assets/materials`.
Os materiais são persistidos em `.bmat` ao salvar o **projeto** e ao usar
**Salvar .bmodel**. O modelo nativo referencia seus materiais por malha, de modo
que materiais e texturas acompanham sua importação e funcionam também no runtime.
Ao transferir um modelo manualmente, mantenha os arquivos referenciados junto dele.
Modelos antigos sem referências de material continuam usando o material padrão.
Cenas antigas com materiais embutidos continuam sendo lidas.

A modelagem oferece três visualizações, guardadas no workflow:

- **Basic**: modelo isolado, superfície neutra e iluminação de trabalho.
- **Material**: modelo isolado com seus materiais, texturas e iluminação de trabalho.
- **Render**: modelo na cena completa, com os demais objetos, luzes, sombras e
  ambiente da câmera do editor. A geometria pendente aparece nessa prévia sem
  alterar a cena antes de salvar.

As três visualizações usam o renderizador OpenGL da engine. A geometria fica na
GPU enquanto não é editada; Basic e Material reutilizam a imagem quando parados.
Render acompanha a cena a cada frame. A seleção calcula a face sob o cursor
somente no clique, sem manter um mapa de faces do tamanho da imagem.
