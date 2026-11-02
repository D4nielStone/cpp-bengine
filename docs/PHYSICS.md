# Física e colisão

O engine usa Bullet para corpos e colisores. O componente `physics` mantém o
corpo Bullet, forma de colisão e estado de contato; o sistema de física aplica
gravidade e atualiza os corpos. A cena salva parâmetros do componente, não
ponteiros Bullet.

## Tipos de corpo

- `rigid`: corpo dinâmico; massa positiva.
- `character`: corpo dinâmico configurado para controle de personagem, com
  rotação angular bloqueada pelo setter atual.
- `static`: massa zero.

Os tipos de forma incluem box, capsule, sphere, cylinder, cone, plane e mesh.
Seleção de forma e opções de colisão são configuradas no Inspector/projeto;
Lua atualmente controla somente as operações de Rigidbody descritas em
[componentes Lua](./lua-api/components.md#rigidbody).

Colisores de malha côncava exigem corpo estático. O engine valida esse
combinação ao carregar/configurar as opções relevantes; objetos de personagem
devem usar um shape primitivo adequado.

## Ajustes usuais

O Inspector permite ajustar forma, dimensões, offset/rotação do colisor,
colisão ativa, trigger, tipo de corpo, massa, atrito e restituição. `fit_to_model`
permite ajustar o shape à geometria quando suportado. Dimensões e offsets são
valores locais da configuração do colisor.

Em Lua, `Rigidbody.velocity`, `AddForce`, `SetLinearFactor`,
`SetAngularFactor`, `mass`, `friction` e `restitution` operam em um componente
já criado. `Destroy(rigidbody)` remove esse componente; não remove a entidade.

## Limites de uso

- Um corpo deve ter transform válido e permanecer associado à entidade durante
  a simulação.
- Configurações de shape e corpo côncavo dependem da geometria e do tipo do
  corpo.
- Remover física durante atualizações deve respeitar as garantias do ECS e do
  sistema; não retenha referências após a remoção.
- A documentação não promete valores universais de estabilidade ou desempenho
  para todo timestep, escala e shape.
