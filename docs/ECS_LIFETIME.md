# Ciclo de vida do ECS

## Entidades

`commons::ecs::create()` cria uma entidade e adiciona sua transformação padrão.
IDs iguais a zero ou já em uso não são reutilizados literalmente: o ECS procura
um ID disponível. `remove(id)` remove a entidade, os componentes, a máscara e
o nome. `is_alive(id)` é a verificação de validade do identificador.

`find_by_name(name)` retorna um ID apenas quando o nome identifica uma única
entidade viva. Nome vazio, ausência ou duplicidade resulta em `nullopt`.

## Componentes e referências

O mapa do ECS é proprietário dos componentes por `shared_ptr`. Componentes
guardam o registry como `weak_ptr`; uma referência compartilhada ao componente
não mantém o ECS vivo. Referências ao componente podem deixar de representar
uma entidade ativa depois de remoção, substituição de componente ou troca de
projeto. Rebusque pelo ID e confira a validade antes de usar em callbacks
assíncronos.

Transformações guardam `parent_id`; relações entre pai e filho precisam apontar
para entidades válidas. A remoção de entidade deve ser considerada ao percorrer
hierarquias e ao reter referências de transformação.

`validate_invariants()` verifica aspectos básicos entre entidades vivas,
componentes e máscaras. Não é uma garantia contra todo estado inválido nem
substitui encapsulamento de acesso aos mapas públicos.

## Recursos gráficos

Câmeras e componentes com recursos GPU devem liberar esses recursos enquanto o
contexto OpenGL apropriado ainda estiver atual. Siga os pontos de liberação
explícitos (`release_graphics_resources`) e o ciclo de vida de `commons::window`.
Não retenha ou destrua recursos gráficos depois de fechar o contexto.

## Lua

`gameObject` e objetos retornados por `GameObject.Find` mantêm uma referência
fraca ao ECS. Se o registry ou a entidade não estiver disponível,
`GetComponent`/`transform` podem retornar `nil`. Não trate um userdata Lua
previamente obtido como autorização para usar indefinidamente o componente.
Consulte a [referência de objetos Lua](./lua-api/objects-and-transforms.md).
