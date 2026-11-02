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
- **Debug**: log de scripts Lua.
- **Editor Diagnostics**: métricas locais de frame e renderização.

O painel **Game** é ocultado enquanto o runtime externo está executando. O
runtime roda em outro processo, portanto a prévia não mostra a simulação desse
processo; ela renderiza o ECS de edição. O runtime atual não fornece sistema de
HUD de jogo. O painel indica essa ausência em seu status.

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
