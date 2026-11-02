# API Lua

Esta página é o ponto de entrada legado e compatível para a referência Lua.
Consulte o [índice da documentação Lua](./lua-api/README.md) para a API
completa, organizada por tópico.

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
