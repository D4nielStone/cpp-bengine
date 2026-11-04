# Lua: primeiros passos

## Associar um script

No editor, crie ou importe um arquivo `.lua` e associe-o a uma entidade pelo
Inspector ou arraste-o para a entidade. O componente `code` armazena o caminho
do script e pode conter scripts adicionais, cada um com seu próprio estado Lua.
Os caminhos do projeto devem ser relativos à raiz do projeto sempre que
possível.

## Scripts prontos

No painel **Arquivos**, abra **Novo script Lua…** e escolha um modelo. O editor
cria uma cópia editável na pasta atual.

- **Movimentação WASD** move no plano global XZ. Com um Rigidbody dinâmico,
  ajusta a velocidade horizontal e preserva a velocidade vertical. Com
  apenas Transform, desloca a entidade usando `Time.deltaTime`.
- **Controle FPP (jogador e câmera)** reúne WASD e rotação X/Y no mesmo
  script. Anexe o script uma vez e selecione `player` e `camera` no Inspector.
  WASD segue a projeção horizontal da câmera. Se a câmera for filha do
  jogador, segue pela hierarquia. Caso contrário, mantém a distância e o
  deslocamento iniciais em relação ao jogador.
- **Jogador FPP (WASD e guinada)** vai no jogador com Rigidbody do tipo
  Personagem. O mouse horizontal gira o jogador e WASD segue essa direção.
- **Câmera FPP (filha do jogador)** vai na entidade com Camera, filha do
  jogador. O mouse vertical controla a inclinação e a câmera herda a guinada.

Use o controle único ou a dupla de scripts FPP por jogador. A velocidade e a sensibilidade ficam no Inspector. Ao criar um projeto com o
template FPP, os scripts de jogador e câmera já vêm associados às entidades.

## Funções de ciclo de vida

```lua
function setup()
    -- Executada uma vez quando este script é inicializado.
end

function update()
    -- Executada em cada atualização do sistema de scripts.
end
```

As duas funções são opcionais. O runtime carrega e executa o arquivo, procura
essas funções globais e chama `setup` antes das atualizações normais. `Time`
começa com `deltaTime == 0` e `time == 0`; antes de cada `update`, o sistema
atualiza ambos. O `deltaTime` entregue ao script é limitado a 0,1 segundo.

Cada script adicional é inicializado e atualizado separadamente. Erros Lua em
inicialização ou atualização são registrados no log e aquele script deixa de
ser atualizado; os demais scripts continuam.

## Exemplo: movimentação horizontal

```lua
local speed = 5.0
local body

function setup()
    body = gameObject:GetComponent("Rigidbody")
    if not body then
        Debug.Log("Esta entidade precisa de um Rigidbody.")
    end
end

function update()
    if not body then return end

    local horizontal = Input.GetAxis("Horizontal")
    local vertical = Input.GetAxis("Vertical")
    local velocity = body.velocity
    body.velocity = Vector3(
        horizontal * speed,
        velocity.y,
        vertical * speed
    )
end
```

Os eixos usam WASD e retornam valores de -1 a 1. Para consultar teclas
individualmente, use `Input.GetKey`; veja [Input e tempo](./input-time-debug.md).

## Exemplo: câmera em uma entidade filha

```lua
local pitch = 0.0

function setup()
    pitch = transform.localEulerAngles.x
    Input.SetMouseCaptured(true)
end

function update()
    local _, mouse_y = Input.GetMouseDelta()
    pitch = math.max(-89.0, math.min(89.0, pitch - mouse_y * 0.12))
    transform.localEulerAngles = Vector3(pitch, 0.0, 0.0)
end
```

Esse exemplo controla apenas o eixo local X. A rotação global pode continuar
sendo herdada do pai. Veja [Transform e objetos](./objects-and-transforms.md).

## Bibliotecas Lua

O estado abre as bibliotecas padrão `base` e `math`. Não presuma que
`io`, `os`, `package`, `debug`, `string`, `table` ou `coroutine` estejam
abertas. Em runtime empacotado, `loadfile` e `dofile` são adaptados para ler
recursos embutidos.

## Coletar uma lanterna

Copie [flashlight_pickup.lua](./examples/flashlight_pickup.lua) para a pasta
`scripts` do projeto e associe-o à entidade da lanterna. Adicione um
`Rigidbody` com colisão habilitada à lanterna para que o raycast possa
detectá-la. No Inspector, atribua as entidades da câmera e do jogador a
`cam` e `player`. Em Play, mire na lanterna e pressione **E**.

Na coleta, a lanterna vira filha do jogador e seu `Rigidbody` é removido,
assim ela acompanha a transformação do jogador. Ajuste `hold_x`, `hold_y`
e `hold_z` no Inspector para posicioná-la no referencial do jogador.

## Próximos tópicos

- [Referência de objetos e Transform](./objects-and-transforms.md)
- [Componentes registrados](./components.md)
- [Propriedades editáveis no Inspector](./script-properties.md)
