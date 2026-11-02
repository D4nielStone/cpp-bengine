# Lua: entrada, tempo e diagnóstico

## `Input`

| Função | Retorno | Contrato |
|---|---|---|
| `Input.GetKey(key)` | boolean | `true` enquanto a tecla mapeada está pressionada. |
| `Input.GetAxis(name)` | número | Eixos `Horizontal` e `Vertical`, derivados de D/A e W/S. Nomes diferentes retornam 0. |
| `Input.GetMouseDelta()` | dois números | Deslocamento X e Y desde a leitura anterior. |
| `Input.SetMouseCaptured(captured)` | nenhum | Captura/oculta o cursor quando verdadeiro; restaura quando falso. |

Teclas aceitas por `GetKey`: letras A–Z e dígitos 0–9 (um caractere), além de
`Space`, `Escape`, `Return`, `Tab`, `LeftShift`, `RightShift`,
`LeftControl`, `RightControl`, `UpArrow`, `DownArrow`, `LeftArrow` e
`RightArrow`. `Space` também aceita formas maiúscula/minúscula. Uma chave não
reconhecida retorna `false`.

```lua
if Input.GetKey("Space") then
    Debug.Log("Space pressionado")
end

local mouse_x, mouse_y = Input.GetMouseDelta()
```

Sem contexto GLFW ativo, a consulta de tecla retorna `false`, o delta do mouse
é `(0, 0)` e a captura não altera uma janela. A primeira leitura do mouse após
inicializar/redefinir a captura retorna delta zero.

## `Time`

| Campo | Tipo | Atualização |
|---|---|---|
| `Time.deltaTime` | número | Segundos desde a atualização anterior; inicia em 0 e é limitado a 0,1. |
| `Time.time` | número | Acumulado dos `deltaTime` entregues a este script; inicia em 0. |

`Time` é atualizado antes de chamar `update`. Portanto, dentro de `setup`, os
valores ainda estão nos padrões.

## `Debug.Log` e `print`

`Debug.Log(value)` grava uma mensagem no log Lua com o identificador da
entidade do script. A função padrão `print(...)` também está registrada pelo
engine, converte cada argumento para texto e envia uma linha ao mesmo sistema
de log.

```lua
Debug.Log("posição: " .. tostring(transform.position.x))
print("speed", 5, true)
```

Erros de `setup` e `update` são registrados com o identificador da entidade.
Uma falha de script impede novas atualizações daquele script durante a vida
da instância do sistema de scripts; ela não encerra automaticamente o processo
do runtime.
