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
    hud:Add("column", "", -1) -- índice 0
    vida = hud:Add("text", "Vida: 100", 0) -- índice 1
    usar = hud:Add("button", "Usar", 0) -- índice 2
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
`row`, `column`, `text`, `button`. O pai é o índice baseado em zero
na ordem de criação, deve existir e ser `row` ou `column`; -1 indica a raiz.
Há um limite de 1024 elementos por HUD. Tipos e pais inválidos geram erro Lua.
Contêineres podem ser aninhados. A raiz organiza as HUDs verticalmente.

`HUD.count` informa a quantidade de elementos e `HUD:Get(índice)` consulta
um elemento existente (nil se o índice for inválido).

Propriedades: `HUD.enabled`; `HudElement.text`, `visible`, `width`, `height`,
`scale`, `position`, `absolute`. Dimensões positivas são unidades de layout BGUI e
respeitam a escala global; zero usa o dimensionamento natural.
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
adicionar texto, botão, linha ou coluna. Clique em um widget para selecioná-lo.
Os controles X/Y movem em cada eixo e W/H redimensionam largura e altura.
O inspetor permite editar ID (único dentro da HUD), título, posição, dimensões,
tamanho da fonte, cor, alinhamento, padding e margin. Confirme textos com Enter.
Widgets criados pelo painel usam coordenadas locais do viewport em unidades BGUI;
layouts antigos continuam usando seu fluxo original. Alterações são salvas na cena.

Para botões, `Lua callback` é o nome de uma função global no script da entidade.
A função recebe o ID do widget e é executada na atualização do script, com erros
reportados no log. Sem callback configurado, `ConsumeClick()` continua disponível.
