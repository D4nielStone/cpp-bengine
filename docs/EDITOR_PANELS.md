# Painéis do editor

Os painéis do editor implementam `editor::ui::editor_panel` e fornecem
`build(bgui::dock&)` e `update()`. `panel_manager` possui os painéis
registrados, cria seus widgets no dock e encaminha atualizações. Os widgets
continuam pertencendo às árvores de elementos do BGUI.

## Painéis principais

- **Project Manager**: abre projetos e cenas existentes. Salve as alterações
  do projeto atual antes de abrir ou criar outro. Se o carregamento falhar,
  o editor mantém a cena ativa e reabre o diálogo com a mensagem de erro.
  Na tela inicial, é possível escolher entre `Empty Project` e
  `FPP - First Person`. O template FPP gera uma cena com jogador cápsula,
  câmera, física e scripts Lua de movimento e controle de câmera. O comando
  **File → New Project** dentro do editor cria um projeto vazio.
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
- **Animação**: trilhas de propriedades por entidade, arquivos de clipe, gravação automática de alterações e edição de keyframes,
  interpolação, prévia com desfazer e reprodução no runtime. Veja
  [o guia de animação](./ANIMATION.md).
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
os padrões de `apps/editor/panels/panel.hpp` e dos painéis existentes.

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

## Criar um painel personalizado com a fachada

A fachada `bgui::panel_facade` simplifica a construção do conteúdo e mantém
os widgets nativos disponíveis. Ela não substitui `editor_panel`,
`panel_manager`, comandos, histórico ou atualização do estado do editor.

```cpp
#include "panel.hpp"
#include <panel_facade.hpp>

class custom_panel final : public editor::ui::editor_panel {
public:
    explicit custom_panel(std::function<void()> apply)
        : m_apply(std::move(apply)) {}

    void build(bgui::dock& parent) override {
        bgui::panel_facade panel(parent, "Ferramentas", bgui::dock_area::right);
        panel.native().add_class("editor-panel");
        auto body = panel.content();
        body.label("Meu painel");
        body.action("Aplicar", m_apply);
    }

    void update() override {}

private:
    std::function<void()> m_apply;
};
```

Registre com `editor_ui::add_panel<custom_panel>(acao)` na inicialização da
interface. `acao` deve encaminhar a operação para os comandos/serviços do
editor. Para propriedades que precisam de histórico e sincronização, mantenha
os helpers de `editor_panel`, passando `body.native()` como layout.

A janela e o conteúdo pertencem ao BGUI e continuam existindo quando a fachada
local sai do escopo. Construir outra fachada cria outro corpo; faça isso uma
vez no `build`, sem reconstrução por frame. O exemplo não captura o objeto do
painel em callbacks. Quando callbacks capturarem `this`, remova a janela antes
de destruir o objeto, seguindo o ciclo de vida dos painéis existentes. Não
use referências de widgets depois de fechar/remover a janela, trocar o layout
raiz ou encerrar BGUI.

Use `row()` e `column()` para agrupar controles, `property()` e `toggle()` para
controles simples e `add<T>()` para widgets especializados. `native()` permite
ajustar classes, dimensões e rolagem com a API atual. Veja a
[referência completa da fachada](../external/cpp-bgui/docs/PANEL_FACADE.md)
para assinaturas, callbacks e limites de ciclo de vida.


Os painéis atuais usam `panel_content` para compor suas superfícies retidas e
`panel_facade` para corpos padrão (Game, Malhas e Animação). Superfícies de
viewport e drivers de atualização especializados são preservados, com a mesma
árvore de widgets e as mesmas camadas de overlay. O gerenciador principal também
usa a fachada de conteúdo; o menu Project Manager retorna a essa interface.

## Grafo de cenas e Lua em blocos

O modo Gestão de cenas inclui **Conexões entre cenas**, **Scripts do projeto** e
**Script em blocos**. O grafo usa chamadas Lua de transição dos scripts associados
às cenas. O editor visual compartilha os documentos do painel Arquivos e grava
Lua após validar a sintaxe. Veja [Gestão visual de cenas e scripts](./SCENE_WORKFLOW.md)
para navegação, encaixes, limites da conversão e recuperação de conflitos.
