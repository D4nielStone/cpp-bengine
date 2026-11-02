# Lua: propriedades de script

Propriedades declaradas no começo de um arquivo `.lua` aparecem no Inspector
como valores editáveis por entidade. São lidas como metadados; o editor não
executa o código Lua para descobrir a lista.

## Sintaxe

Cada propriedade é uma linha de comentário com um objeto JSON:

```lua
--@property {"name":"speed","type":"number","default":5}
--@property {"name":"enabled","type":"boolean","default":true}
--@property {"name":"title","type":"text","default":"Player"}
--@property {"name":"tint","type":"color","default":[1,0.5,0,1]}
--@property {"name":"icon","type":"image","default":"assets/icon.png"}
```

Espaços antes da marca são aceitos. Os nomes devem começar com letra ASCII ou
`_`, e conter somente letras, dígitos e `_`. Não pode haver nomes repetidos no
mesmo script.

## Tipos aceitos

| `type` | `default` JSON | Valor em `properties` |
|---|---|---|
| `number` | Número JSON finito | Número Lua. |
| `boolean` | `true` ou `false` | Booleano Lua. |
| `text` | String | String Lua. |
| `image` | String | Caminho de imagem; no projeto, caminhos relativos são resolvidos pela raiz do projeto para o valor entregue a Lua. |
| `color` | Array de quatro números entre 0 e 1 | Userdata `Color`, com `r`, `g`, `b`, `a`. |

Tipos desconhecidos, JSON inválido, valores de tipo incorreto, cores fora do
intervalo e nomes repetidos causam erro de metadados, identificado com caminho
do arquivo e linha. Valores numéricos devem ser finitos.

## Acessar em Lua

O objeto global `properties` existe antes da execução de código de topo e de
`setup`:

```lua
--@property {"name":"speed","type":"number","default":5}

local observed_speed = properties.speed

function setup()
    observed_speed = properties.speed
end

function update()
    -- Leia a propriedade durante a atualização para usar o valor atual.
    transform:Translate(Vector3(properties.speed * Time.deltaTime, 0, 0))
end
```

O valor salvo no componente prevalece sobre o `default` quando o script é
carregado novamente e o tipo/nome continuam compatíveis. Cada entidade e cada
script adicional mantém seus próprios valores; editar a propriedade de uma
instância não altera as outras.

Imagens são recursos do projeto. Use caminhos relativos importados para que
projetos e exportações permaneçam portáteis.

Para a documentação anterior, veja também
[LUA_PROPERTIES.md](../LUA_PROPERTIES.md).
