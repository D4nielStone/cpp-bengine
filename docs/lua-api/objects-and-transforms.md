# Lua: objetos e transformações

## `gameObject`

Cada script recebe `gameObject`, o objeto da entidade à qual o componente de
código pertence.

| Membro | Tipo | Contrato |
|---|---|---|
| `gameObject.id` | inteiro | Identificador da entidade; somente leitura. |
| `gameObject.transform` | `Transform` ou `nil` | Transformação da própria entidade. |
| `gameObject:GetComponent(name)` | componente ou `nil` | Busca um componente existente pelo nome registrado. |

Nomes aceitos por `GetComponent`:
`Transform`, `Renderer`, `Camera`, `DirectionalLight`, `PointLight`,
`SpotLight`, `SoundListener`, `SoundPlayer` e `Rigidbody`.
Um componente ausente ou nome desconhecido resulta em `nil`.

## `GameObject.Find`

`GameObject.Find(id)` busca pelo identificador numérico. `GameObject.Find(name)`
busca pelo nome atribuído à entidade. Ambos retornam um objeto ou `nil`.

```lua
local player = GameObject.Find("Player")
if player and player.transform then
    player.transform.localPosition = Vector3(0, 2, 0)
end
```

Nomes vazios, entidades removidas e nomes duplicados não identificam uma
entidade válida. O valor retornado é uma referência não proprietária: uma
entidade pode desaparecer depois da busca; consulte novamente o membro ou
componente antes de usá-lo.

## `Vector3`

`Vector3()` cria `(0, 0, 0)`; `Vector3(x, y, z)` cria um vetor com os valores
fornecidos. Os campos `x`, `y` e `z` podem ser lidos e escritos.

```lua
local offset = Vector3(1, 0, -2)
offset.y = 3
```

## `Transform`

`Transform` não pode ser construído diretamente. Obtenha-o de
`gameObject.transform` ou `GetComponent("Transform")`.

| Propriedade/método | Acesso | Semântica |
|---|---|---|
| `position` | leitura/escrita | Posição global; a escrita usa `set_world_position`. |
| `localPosition` | leitura/escrita | Posição relativa ao pai. |
| `eulerAngles` | leitura/escrita | Rotação global em ângulos Euler. |
| `localEulerAngles` | leitura/escrita | Rotação local em ângulos Euler. |
| `localScale` | leitura/escrita | Escala local. |
| `Translate(offset)` | método | Move pela translação indicada no referencial da entidade; o offset é rotacionado pela orientação local atual. |
| `Rotate(offset)` | método | Aplica rotação pelo método `transform::rotate` do engine. |

Os vetores de rotação usam graus. Para mover no espaço global sem aplicar a
orientação local, altere `position` diretamente. Em entidades hierárquicas,
`position` e `eulerAngles` são globais; os membros `local*` são relativos.

## `Color`

`Color()` cria a cor padrão do tipo; `Color(r, g, b, a)` constrói uma cor.
`r`, `g`, `b` e `a` são campos legíveis e graváveis.

```lua
local tint = Color(1.0, 0.5, 0.0, 1.0)
```

Não há conversão ou clamp genérico documentado para componentes criados em Lua.
Propriedades Lua do tipo `color` no Inspector validam canais entre 0 e 1; isso
é um contrato separado, descrito em [propriedades de script](./script-properties.md).

## `Destroy`

`Destroy(component)` remove o `Rigidbody` associado ao componente recebido:

```lua
local body = gameObject:GetComponent("Rigidbody")
if body then Destroy(body) end
```

Apesar do nome, essa função não remove a entidade nem oferece destruição
genérica de outros componentes. A operação é segura para `nil` e usa o ECS ao
qual o componente pertence.
