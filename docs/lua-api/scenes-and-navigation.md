# Cenas e navegação de inimigos

## SceneManager

Disponível nos scripts executados pelo runtime de um projeto (incluindo Play do
editor, que abre o runtime). Use ponto nas chamadas:

| Chamada | Retorno |
|---|---|
| `SceneManager.LoadScene("assets/levels/arena.scene")` | `true` se a solicitação foi enfileirada |
| `SceneManager.ReloadScene()` | Enfileira a cena ativa novamente |
| `SceneManager.GetActiveScene()` | Caminho da cena relativo à pasta do `.bengine` |
| `SceneManager.IsLoading()` | Há uma solicitação pendente/em processamento |
| `SceneManager.GetLastError()` | Último erro; string vazia quando não há erro |

Os caminhos são relativos à pasta do projeto, com extensão `.scene` ou `.world`.
Caminhos absolutos, externos ao projeto ou inexistentes são rejeitados. Apenas
uma solicitação fica pendente. A troca ocorre entre frames, depois que a execução
do script retorna; `true` significa aceitação, não conclusão do carregamento.
Erros de leitura/validação são enviados ao log e preservam a cena anterior.
`GetLastError()` também permite consultar a falha a partir dos scripts ativos.

A troca substitui as entidades da cena e reinicia seus scripts, física e câmera.
Referências guardadas para objetos/componentes da cena anterior devem ser
abandonadas; procure os objetos novamente no `setup()` da nova cena. Não há
entidades persistentes entre cenas nem carregamento aditivo. A troca em runtime
não salva o estado do jogo e não altera a cena inicial no manifesto. Os mesmos
caminhos funcionam nos recursos empacotados no executável.

```lua
function update()
    if Input.GetKey("N") and not SceneManager.IsLoading() then
        if not SceneManager.LoadScene("assets/levels/arena.scene") then
            Debug.Log(SceneManager.GetLastError())
        end
    end
end
```

Fora de um runtime de projeto, as consultas retornam cena vazia e `false` para
carregamento; `GetLastError()` informa a indisponibilidade. A API não abre nem
substitui o projeto do editor.

## Componente Pathfinder

Adicione **Pathfinder** no Inspector do inimigo. O componente planeja uma rota
A* em uma grade XZ, usando as caixas delimitadoras mundiais dos colisores da
cena. Ignora o próprio inimigo, triggers e colisores desativados. O raio do
agente expande os obstáculos. Os segmentos completos são testados para impedir
que paredes finas sejam atravessadas entre células. Colisores que apenas tocam
o plano de navegação (como o piso) não bloqueiam a rota.

| Chamada | Comportamento |
|---|---|
| `gameObject:GetComponent("Pathfinder")` | Componente ou `nil` |
| `path:Configure(cellSize, agentRadius, searchRadius)` | Valida opções, limpa a rota e retorna sucesso |
| `path:FindPath(Vector3(x, y, z))` | Calcula a rota; retorna `false` e limpa a anterior se não houver caminho válido |
| `path:GetPath()` | Cópia dos pontos em uma tabela Lua, índices a partir de 1 |
| `path:Clear()` | Limpa a rota exibida |

Opções também disponíveis no Inspector e persistidas na cena: tamanho de célula
`0.05–100` (padrão `0.5`), raio do agente `0–100` (padrão `0.25`) e raio da busca
`1–64` células (padrão `32`). A grade é centrada na posição atual do inimigo,
com até 129 × 129 células. A rota não é salva: deve ser recalculada no runtime.
Referências a um Pathfinder removido não podem calcular nem fornecer rotas.

Pressione **F3** no runtime de diagnóstico para mostrar a rota em laranja junto
aos colisores. A mesma geometria está disponível na visualização de colisores
do editor; a rota existe no ECS que a calculou. Builds de distribuição que
desativam os diagnósticos também desativam esse overlay.

O componente fornece o caminho; o controlador Lua decide como mover o inimigo.
Este exemplo usa Rigidbody e recalcula periodicamente para acompanhar o jogador:

```lua
local route, waypoint, elapsed = {}, 2, 1

function update()
    local player = GameObject.Find("Player")
    local path = gameObject:GetComponent("Pathfinder")
    local body = gameObject:GetComponent("Rigidbody")
    if not player or not path or not body then return end

    elapsed = elapsed + Time.deltaTime
    if elapsed >= 0.5 then
        elapsed = 0
        local target = player.transform.position
        target.y = transform.position.y
        if path:FindPath(target) then route = path:GetPath()
        else route = {} end
        waypoint = 2 -- o primeiro ponto é a posição inicial
    end

    local position = transform.position
    while route[waypoint] and Vector3.distance(position, route[waypoint]) < 0.15 do
        waypoint = waypoint + 1
    end
    local direction = route[waypoint] and (route[waypoint] - position):normalized() or Vector3()
    body.velocity = Vector3(direction.x * 2, body.velocity.y, direction.z * 2)
end
```

A busca é síncrona: evite chamá-la a cada frame para muitos inimigos. Recalcule
quando o destino ou os obstáculos mudarem. Esta navegação é planar: não oferece
navmesh, rampas, degraus, saltos, previsão de obstáculos móveis ou desvio entre
agentes. O destino deve ter a mesma altura da origem (tolerância `0.01`). AABB
é conservadora para colisores rotacionados e modelos côncavos. Ajuste o raio e o
plano de navegação ao colisor e ao controlador físico do inimigo.

O editor também oferece um [grafo das transições e edição de scripts em blocos](../SCENE_WORKFLOW.md)
no modo Gestão de cenas. As setas são inferidas das chamadas Lua literais, sem
executar os scripts.
