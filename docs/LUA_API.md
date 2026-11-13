# API Lua

Esta página é o ponto de entrada legado e compatível para a referência Lua.
Consulte o [índice da documentação Lua](./lua-api/README.md) para a API
completa, organizada por tópico.

## Céu padrão

Novas câmeras usam `Color(0.16, 0.16, 0.16, 1)` em `Camera.backgroundColor`,
com zênite também cinza. Cores explícitas de cenas existentes são preservadas.
Os templates Empty/FPP herdam esse padrão; o comando visual de cor da câmera
e a dica da conclusão de código usam o mesmo valor.

## Início rápido

```lua
local move_speed = 5.0

function setup()
    local body = gameObject:GetComponent("Rigidbody")
    if body then
        Debug.Log("Controlador pronto para " .. gameObject.id)
    end
end

function update()
    local body = gameObject:GetComponent("Rigidbody")
    if not body then return end

    local forward = Input.GetAxis("Vertical")
    body.velocity = Vector3(0, body.velocity.y, forward * move_speed)
end
```

Consulte:

- [Primeiros passos e ciclo de vida](./lua-api/getting-started.md)
- [Objetos, transformações e funções globais](./lua-api/objects-and-transforms.md)
- [Componentes disponíveis](./lua-api/components.md)
- [Entrada, tempo e depuração](./lua-api/input-time-debug.md)
- [Propriedades configuráveis no Inspector](./lua-api/script-properties.md)
- [Compatibilidade, recursos e limitações](./lua-api/compatibility.md)

## Sombras em tempo real

Luzes direcionais e holofotes expõem propriedades de sombra listadas na
[referência de componentes](./lua-api/components.md). A API Lua
reflete os componentes que existem no código; ela não cria luzes nem entidades.

## HUD

`gameObject:AddHUD()` cria ou recupera a HUD; `GetComponent("HUD")` consulta.
Veja [HUD modular](HUD.md) para tipos, layout, botões e exemplos.

## Animações e consulta no editor

`GetComponent("Animation")` expõe reprodução, pausa, busca e transições por
`CrossFade`. Veja [controle Lua e blending](./ANIMATION.md#controle-lua-e-blending).
Na barra de menu, **API Reference** indexa recursivamente os arquivos
`.md` e `.markdown` de `docs/`, agrupa os resultados pela pasta e permite
pesquisar tanto nomes quanto conteúdo. Selecione um documento para lê-lo no
editor; novos guias Markdown aparecem no catálogo sem uma lista de tópicos
codificada manualmente.

- [Gerenciamento de cenas e Pathfinder para inimigos](./lua-api/scenes-and-navigation.md)

O modo **Gestão de cenas → Script em blocos** também oferece a API em uma
paleta por categorias, com busca e argumentos editáveis. Por exemplo,
`Translate(Vector3(x, y, z))` aparece como **Mover**, com alvo e três campos.
Veja [edição visual de scripts](./SCENE_WORKFLOW.md#script-em-blocos).
