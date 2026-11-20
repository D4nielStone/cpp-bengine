# HUD modular

Adicione **HUD** pelo menu de componentes do inspetor ou use
`gameObject:AddHUD()` em Lua. `GetComponent("HUD")` retorna o componente
existente; `AddHUD()` é idempotente e retorna nil para entidades removidas.

A HUD usa os widgets, layout e roteamento de entrada BGUI do viewport.
É exibida no runtime e no painel Game do editor, limitada ao viewport.
Não cria janelas, docks ou modais e não possui recursos OpenGL próprios.
A configuração é salva na cena e preservada ao copiar entidades.

```lua
local hud, vida, usar
function setup()
    hud = gameObject:AddHUD()
    if hud.count > 0 then
        vida = hud:Get(1)
        usar = hud:Get(2)
        return
    end
    local panel = hud:Add("column", "", -1) -- índice 0
    panel.width, panel.height = 180, 100
    vida = hud:Add("text", "Vida: 100", 0) -- índice 1
    usar = hud:Add("button", "Usar", 0) -- índice 2
    usar.position = Vector3(0, 50, 0) -- posição relativa ao panel
    usar.width = 160
    usar.height = 36
end
function update()
    if usar:ConsumeClick() then
        vida.text = "Botão acionado"
    end
end
```

`HUD:Add(tipo, texto, pai)` retorna um `HudElement`. Tipos disponíveis:
`row`, `column`, `text`, `button`, `image`. O pai pode ser o índice baseado em zero
na ordem de criação ou seu ID; deve existir antes do filho. Qualquer widget
pode conter outros widgets; -1 indica a raiz. Omissão do pai cria na raiz.
Há um limite de 1024 elementos por HUD. Tipos e pais inválidos geram erro Lua.
Contêineres podem ser aninhados. Novos widgets usam posicionamento livre relativo ao pai, com tamanho inicial
160 × 40 unidades de layout. Para composição em fluxo, use linhas/colunas e
`absolute=false` nos filhos. A raiz organiza verticalmente os widgets em fluxo.

`HUD.count` informa a quantidade de elementos e `HUD:Get(índice)` consulta
um elemento existente (nil se o índice for inválido).

Propriedades: `HUD.enabled`; `HudElement.text`, `visible`, `width`, `height`,
`scale`, `position`, `absolute`, `relative` (true em novos widgets). Dimensões positivas são unidades de layout BGUI e
respeitam a escala global; em fluxo, zero usa o dimensionamento natural.
No posicionamento livre, use dimensões positivas.
`scale` controla o tamanho da fonte. `ConsumeClick()` retorna true uma vez
por clique pendente, sem chamar scripts durante a travessia da interface.
Referências Lua retidas mantêm os dados vivos, mas uma HUD removida do ECS
não aparece nem recebe novos cliques.

## Fazer um texto acompanhar um ponto da cena

Projete a posição global com `camera:worldToScreen` e passe o resultado para
`widget:SetScreenPosition(pixel)`. O método ativa o posicionamento absoluto
e converte os pixels do viewport em unidades BGUI. `x` e `y` definem o canto
superior esquerdo do widget. Defina `width` e `height` para dimensionar o
texto em modo absoluto. Use a profundidade `z` e os limites do viewport
para ocultar o texto quando o ponto sai da imagem. O
[exemplo da lanterna](lua-api/examples/flashlight_pickup.lua) mostra um aviso
**E** acima do objeto e o oculta após a coleta.

Cenas com `text3d` convertem esses widgets em texto 2D no layout do HUD.
Ao salvar, a posição, rotação, escala e profundidade 3D originais ficam
arquivadas no campo `legacy_world_text`.

A leitura de cenas valida a árvore, dimensões e posições antes de substituir
a configuração; eventos de clique não são persistidos. O layout deve ser
criado no thread da interface, como os demais componentes do motor.

## Edição no painel Game

Fora de Play, clique com o botão direito em uma área vazia de Game para
adicionar texto, botão, imagem, linha ou coluna. Com um widget selecionado,
o novo elemento é criado como seu filho. Clique em um widget para selecioná-lo.
Os controles X/Y movem em cada eixo e W/H redimensionam largura e altura.
O inspetor permite editar ID (único dentro da HUD), título, posição, dimensões,
tamanho da fonte, cor, alinhamento, padding, margin, pai, opacidade, fonte, imagem e shader. Confirme textos com Enter.
Widgets criados pelo painel usam coordenadas locais do pai em unidades BGUI;
layouts antigos continuam usando seu fluxo original. Alterações são salvas na cena.

Para botões, `Lua callback` é o nome de uma função global no script da entidade.
A função recebe o ID do widget e é executada na atualização do script, com erros
reportados no log. Sem callback configurado, `ConsumeClick()` continua disponível.

## Aparência e acesso por ID

`hud:Find("health")` e `hud:Get("health")` retornam o widget ou nil.
`Get(índice)` permanece disponível. `id` deve ser não vazio e único na HUD;
`callback` define o nome da função Lua chamada no clique do botão.
Veja o [exemplo completo](lua-api/examples/modular_hud.lua).

O padrão de novos widgets é `absolute=true` e `relative=true` (layout livre
modular). `absolute=false` seleciona o fluxo BGUI de linhas/colunas.
Em coordenadas livres, `relative=true` posiciona em relação ao canto superior
esquerdo do pai; sem pai, a referência é o viewport. `relative=false` mantém
coordenadas do viewport. `SetScreenPosition` ativa esse último modo para
preservar projeções de pontos da cena. Cenas antigas sem `relative` continuam
com sua referência original; a ausência de `absolute` preserva o fluxo antigo.
Scripts que desejem o fluxo anterior ao criar novos widgets devem definir
`absolute=false` e, se necessário, `width=0`/`height=0` para tamanho natural. O inspetor permite escolher o pai dentre widgets
anteriores, evitando ciclos; não exige que o pai seja um contêiner.

Propriedades de aparência disponíveis em Lua e no inspetor:

- `color`: `Color(r,g,b,a)`, cor do texto ou tonalidade da imagem.
- `opacity`: 0 a 1, multiplicada pela opacidade dos ancestrais. Ocultar um pai
  também oculta seus filhos e impede interação com eles.
- `background`, `hoverColor`, `pressedColor`, `borderColor`: cores RGBA.
- `borderSize`, `borderRadius`, `padding`, `margin`: unidades de layout.
- `alignment`: 0 (início), 1 (centro), 2 (fim).
- `font`: caminho de fonte `.ttf` ou `.otf`; vazio restaura a fonte padrão.
- `image`: caminho PNG/JPEG para widget `image` ou textura de botão.
- `shader`: caminho de fragmento GLSL; em botões aplica ao texto do rótulo.

Importe os arquivos pelo painel Files e use caminhos relativos à raiz do
projeto. Os campos do inspetor recebem esses caminhos. Arquivos ausentes ou
inválidos geram mensagens no log; fontes e shaders usam a aparência padrão,
e imagens inválidas ficam vazias. Cores usam valores de 0 a 1; tamanhos não
negativos até 10000. Valores inválidos impedem salvar a configuração.
Cores retornadas a Lua são cópias: atribua `widget.color = Color(...)` para
aplicar a alteração. Configurações são preservadas em save/load e copy/paste.
Empty e FPP não criam HUD automaticamente e mantêm os controles existentes.

## Fragmentos personalizados (OpenGL 3.3)

O backend BGUI reutiliza a geometria, o atlas de fonte, o recorte e o estado
OpenGL da UI. O arquivo deve conter apenas o fragmento, sem `#version` nem
redeclaração do bloco de material. Estão disponíveis `in vec2 Uv`,
`in vec2 TexCoord`, `flat in vec2 RectSize`, `uniform sampler2D tex` e
`uniform float hudTime` (segundos desde o primeiro desenho personalizado).
O bloco `BguiMaterial` é acrescentado com `bg_color`, `border_color`,
`text_color`, `border_style` e `instance_flags`. Declare apenas as entradas
usadas pelo fragmento. Para texto, a cobertura do glifo está no canal vermelho
de `texture(tex, TexCoord)`; multiplique a cobertura por `text_color.a`.

No painel Files, “Novo shader GLSL...” → “Texto da HUD” cria um fragmento
compatível pronto para editar.

O [fragmento de pulsação](lua-api/examples/text_pulse.frag) demonstra o contrato.
Shaders compilados ficam em cache até reiniciar o backend; editar o mesmo
arquivo exige reiniciar Play/aplicação com recriação do backend (parar Play
sozinho não recria o contexto do editor). Falhas são registradas uma vez por
caminho e usam o shader padrão. Este contrato é específico do backend OpenGL;
não há implementação de fragmentos GLSL personalizados no backend Vulkan.

O alinhamento 0/1/2 (Start/Center/End) também se aplica à superfície de texto,
incluindo títulos de abertura. Fontes `.ttf` do projeto podem ser atribuídas
pela propriedade `font`. Opacidade e visibilidade permitem transições por Lua.
