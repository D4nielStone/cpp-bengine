# HUD modular e texto 3D

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
    local objetivo = hud:Add("text3d", "Objetivo", -1)
    objetivo.position = Vector3(0, 2, 0)
end
function update()
    if usar:ConsumeClick() then
        vida.text = "Botão acionado"
    end
end
```

`HUD:Add(tipo, texto, pai)` retorna um `HudElement`. Tipos disponíveis:
`row`, `column`, `text`, `button`, `text3d`. O pai é o índice baseado em zero
na ordem de criação, deve existir e ser `row` ou `column`; -1 indica a raiz.
Há um limite de 1024 elementos por HUD. Tipos e pais inválidos geram erro Lua.
Contêineres podem ser aninhados. A raiz organiza as HUDs verticalmente.

`HUD.count` informa a quantidade de elementos e `HUD:Get(índice)` consulta
um elemento existente (nil se o índice for inválido).

Propriedades: `HUD.enabled`; `HudElement.text`, `visible`, `width`, `height`,
`scale`, `position`. Dimensões positivas são unidades de layout BGUI e
respeitam a escala global; zero usa o dimensionamento natural.
`scale` controla o tamanho da fonte. `ConsumeClick()` retorna true uma vez
por clique pendente, sem chamar scripts durante a travessia da interface.
Referências Lua retidas mantêm os dados vivos, mas uma HUD removida do ECS
não aparece nem recebe novos cliques.

`text3d` é um rótulo de tamanho constante na tela, ancorado em coordenadas
mundiais absolutas (`position`), projetado pela câmera do viewport. Ele é
ocultado fora do volume de visão e atrás da câmera. Não é uma malha de texto:
não tem rotação, perspectiva dos glifos nem oclusão por objetos da cena.
O texto usa a fonte da BGUI; sua cor pode ser configurada no inspetor.

A leitura de cenas valida a árvore, dimensões e posições antes de substituir
a configuração; eventos de clique não são persistidos. O layout deve ser
criado no thread da interface, como os demais componentes do motor.

## Edição no painel Game

Fora de Play, clique com o botão esquerdo em uma área vazia de Game para
adicionar texto, botão, linha ou coluna. Clique em um widget para selecioná-lo.
Os controles X/Y movem em cada eixo e W/H redimensionam largura e altura.
O inspetor permite editar ID (único dentro da HUD), título, posição, dimensões,
tamanho da fonte, cor, alinhamento, padding e margin. Confirme textos com Enter.
Widgets criados pelo painel usam coordenadas locais do viewport em unidades BGUI;
layouts antigos continuam usando seu fluxo original. Alterações são salvas na cena.

Para botões, `Lua callback` é o nome de uma função global no script da entidade.
A função recebe o ID do widget e é executada na atualização do script, com erros
reportados no log. Sem callback configurado, `ConsumeClick()` continua disponível.
