# Lua: componentes

Todos os componentes abaixo são consultados em uma entidade existente com
`gameObject:GetComponent("Nome")`. `GetComponent` retorna `nil` se o componente
não existir. Lua não oferece uma função geral para adicionar componentes ou
entidades.

## `Renderer`

| Campo | Acesso | Observação |
|---|---|---|
| `enabled` | leitura/escrita | Alias de `visible`. |
| `visible` | leitura/escrita | Ativa ou oculta a renderização da entidade. |
| `castShadows` | leitura/escrita | Controla se o Renderer projeta sombras. |
| `receiveShadows` | leitura/escrita | Controla se recebe sombras. |

`enabled` e `visible` referem-se ao mesmo estado.

## `Rigidbody`

| Campo/método | Acesso | Contrato |
|---|---|---|
| `mass` | leitura/escrita | Escrita usa `setMass`; valores não positivos tornam o corpo estático. |
| `friction` | leitura/escrita | Coeficiente de atrito. |
| `restitution` | leitura/escrita | Coeficiente de restituição. |
| `velocity` | leitura/escrita | Velocidade linear, como `Vector3`. A escrita ativa o corpo. |
| `AddForce(vector)` | método | Aplica força central e ativa o corpo. |
| `SetLinearFactor(vector)` | método | Define os fatores de movimento linear. |
| `SetAngularFactor(vector)` | método | Define os fatores de movimento angular. |

Esses métodos operam em um corpo já existente. A API Lua não configura shape,
colisor, massa do corpo de forma inicial, nem troca seu tipo para character.
Consulte [Física e colisão](../PHYSICS.md) para os tipos usados pelo editor e
pelo formato de cena.

## `Camera`

| Propriedade | Acesso | Semântica |
|---|---|---|
| `forward` | leitura | Direção mundial atual da visão, como `Vector3`. |
| `fieldOfView` | leitura/escrita | Campo de visão em graus. |
| `aspect` | somente leitura | Aspecto calculado/armazenado pela câmera. |
| `nearClipPlane` | leitura/escrita | Plano próximo. |
| `farClipPlane` | leitura/escrita | Plano distante. |
| `orthographic` | leitura/escrita | Ativa projeção ortográfica. |
| `orthographicSize` | leitura/escrita | Escala ortográfica. |
| `backgroundColor` | leitura/escrita | Cor sólida de fundo (`Color`). |
| `useSkybox` | leitura/escrita | Ativa o skybox da câmera. |
| `skyboxImage` | leitura/escrita | Caminho da imagem de skybox. |
| `fogEnabled` | leitura/escrita | Ativa neblina. |
| `fogColor` | leitura/escrita | Cor da neblina (`Color`). |
| `fogDensity` | leitura/escrita | Densidade da neblina. |

O binding escreve diretamente nas configurações da câmera; não faz validação
adicional para cada atribuição Lua. A validade final depende do uso pelo
renderizador.

| Método | Resultado |
|---|---|
| `camera:worldToScreen(position)` | `Vector3(x, y, depth)` para uma posição global, ou `nil` se a projeção for inválida ou o ponto estiver atrás de uma câmera em perspectiva. |
| `camera:screenToWorld(Vector3(x, y, depth))` | Posição global como `Vector3`, ou `nil` se a conversão for inválida. |
| `camera:traceRay(maxDistance, ignore, x, y)` | Primeiro impacto no mundo de física, ou `nil`. O impacto contém `entity` (`GameObject`), `point`, `normal` e `distance`. |

As coordenadas de tela são pixels da imagem da câmera, com origem no canto
superior esquerdo. A profundidade projetada vai de 0 no plano próximo a 1 no
plano distante. Pontos fora da imagem podem produzir valores fora desses
limites. A profundidade em perspectiva varia de forma não linear.

`traceRay()` usa o centro da imagem e alcance de 300 unidades. Passe um
`GameObject` como segundo argumento para ignorar seu colisor, como no caso
do jogador. Para mirar em outro ponto, informe os dois pixels e use `nil`
no segundo argumento se desejar incluir todos os colisores. A distância é
medida a partir da câmera em perspectiva ou do plano próximo em ortográfica.
A consulta usa os colisores ativos do runtime e retorna o impacto mais
próximo, inclusive em sensores com colisão habilitada.

Veja o [exemplo de coleta de lanterna](./getting-started.md#coletar-uma-lanterna).

## Iluminação

`DirectionalLight` expõe campos graváveis `direction`, `ambient`, `color`
(vetores), `intensity`, `shadows`, `shadowDistance`, `shadowBias` e
`shadowStrength`.

`PointLight` expõe `position`, `color` (vetores), `intensity`, `constant`,
`linear` e `quadratic`.

`SpotLight` expõe os campos de ponto `position`, `color`, `intensity`,
`constant`, `linear` e `quadratic`, além de `direction`, `worldPosition`,
`worldDirection`, `innerCutoff`, `outerCutoff`, `shadows`, `shadowDistance`,
`shadowBias` e `shadowStrength`. Também oferece `GetWorldDirection()`,
`SetInnerCutoff(value)` e `SetOuterCutoff(value)`. Os métodos/setters de cutoff
limitam os ângulos para manter `innerCutoff < outerCutoff`.

As cores dos componentes de iluminação são `Vector3`, não `Color`. A API Lua
não registra membros para alterar resolução de sombra.

## Áudio

`SoundListener` expõe `enabled`, leitura/escrita.

`SoundPlayer` expõe os campos leitura/escrita `clip`, `volume`, `pitch`,
`radius`, `looping` e `playOnAwake`. Esses campos editam a configuração do
componente. A API atualmente não registra `Play`, `Stop`, pausa, nem controle
de reprodução por script.
