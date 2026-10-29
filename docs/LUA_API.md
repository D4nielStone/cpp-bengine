# API Lua

Scripts continuam usando `setup()` e `update()`. A API `bubble.*` existente é
preservada. Os nomes abaixo aproximam a ergonomia da Unity, com um subconjunto
explícito de recursos; não são uma implementação da API inteira da Unity.

```lua
local body
function setup()
    body = gameObject:GetComponent("Transform")
    body.localPosition = Vector3(0, 1, 0)
    Debug.Log("Pronto")
end
function update()
    body:Translate(Vector3(0, 0, Input.GetAxis("Vertical") * Time.deltaTime))
end
```

- `gameObject.id`: ID da entidade do script.
- `gameObject.transform` e `transform`: Transform da entidade; ausente retorna `nil`.
- `gameObject:GetComponent("Transform")`: consulta o Transform. Também aceita `"Renderer"`
  (`enabled`/`visible`, visibilidade), `"Rigidbody"`, `"Camera"`,
  `"DirectionalLight"` e `"PointLight"`. Outros nomes retornam `nil`.
  `Destroy(body)` remove um Rigidbody do ECS; o sistema de física sincroniza
  a remoção na próxima atualização.
- `GameObject.Find(id)`: procura por ID numérico e retorna um objeto ou `nil`.
  A busca por nome ainda não está disponível.
- `Vector3(x, y, z)` / `Vector3()`: vetor com campos `x`, `y`, `z`.
- Transform: `position` e `eulerAngles` em coordenadas globais;
  `localPosition`, `localEulerAngles` e `localScale` em coordenadas locais.
  Atribua o vetor inteiro (`transform.position = Vector3(...)`); os vetores
  retornados são cópias. Ângulos em graus.
- `transform:Translate(v)`: deslocamento nos eixos locais do objeto.
  `transform:Rotate(v)`: soma aos ângulos de Euler locais.
- `Time.deltaTime`: intervalo da atualização, em segundos, igual a
  `bubble.delta_time`, limitado pelo runtime a 0,1 s. `Time.time`: soma desses
  intervalos desde a inicialização do script; ambos começam em zero.
- `Input.GetKey(key)`: tecla pressionada; letras, números, `Space`, `Escape`,
  `Return`, `Tab`, `LeftShift`, `RightShift`, `LeftControl`, `RightControl`,
  `UpArrow`, `DownArrow`, `LeftArrow`, `RightArrow`. Sem janela ativa retorna falso.
- `Input.GetAxis("Horizontal")`: D menos A; `"Vertical"`: W menos S.
  São valores digitais (-1, 0, 1), sem suavização. Eixos desconhecidos retornam zero.
- `Input.GetMouseDelta()`: retorna os deltas x/y do mouse; sem janela, ambos zero.
- `Input.SetMouseCaptured(bool)`: captura ou libera o cursor.
- `Debug.Log(text)`: escreve no log do motor.

## Editor de texto

Cada arquivo abre em uma aba da dock. `*` indica conteúdo diferente da versão
salva. Ctrl+S salva o arquivo focado; desfazer até a versão salva também remove
`*`. Não há barra duplicada de botões de arquivo/salvar nem guias de escopo.

Em arquivos `.lua`, `.` ou `:` abre sugestões da API conhecida do motor,
filtradas pelo prefixo digitado. Use ↑/↓ e Tab/Enter, ou clique para aceitar;
Esc fecha. A inserção participa do histórico de desfazer. Aliases simples de
`transform`, `gameObject` e `GetComponent("Transform")` são reconhecidos.
Comentários, strings e objetos desconhecidos não oferecem sugestões. Esta é
uma análise local pequena: não resolve classes arbitrárias definidas pelo
usuário, módulos, escopos complexos, sobrecargas ou tipos externos. O catálogo
precisa acompanhar futuras adições à API.

## Exemplo FPP

Os três scripts em `projects/fpp_demo/scripts` usam a nova API: movimento e yaw
no jogador, pitch local da câmera e coleta do pickup. O movimento preserva a
convenção de yaw da câmera do motor (+X em zero graus), calculando o deslocamento
antes de atribuir `localPosition`. A coleta usa `Renderer.enabled` e `Destroy`
no Rigidbody, sem chamadas `bubble.*`. Testes executam os scripts reais com
entrada simulada, sem precisar de uma janela.

## Propriedades dos componentes

Use `gameObject:GetComponent("Nome")` e atribua propriedades diretamente:

```lua
local light = gameObject:GetComponent("DirectionalLight")
light.color = Vector3(1, 0.8, 0.5)
light.intensity = 2
local body = gameObject:GetComponent("Rigidbody")
body.mass = 3
body.velocity = Vector3(0, 2, 0)
```

- Rigidbody: `mass`, `friction`, `restitution`, `velocity`; métodos `AddForce(v)`,
  `SetLinearFactor(v)` e `SetAngularFactor(v)`. Os setters atualizam o corpo Bullet.
- Camera: `fieldOfView`, `aspect`, `nearClipPlane`, `farClipPlane`, `orthographic`,
  `orthographicSize`, `backgroundColor` (`Color(r, g, b, a)`) e `useSkybox`.
  O aspecto continua sendo atualizado pelo tamanho do viewport.
- DirectionalLight: `direction`, `ambient`, `color`, `intensity`. A direção é local:
  a rotação do Transform e de seus ancestrais é aplicada ao renderizar, sem
  influência da posição ou escala e sem alterar a direção armazenada.
- PointLight: `position`, `color`, `intensity`, `constant`, `linear`, `quadratic`.

O painel Debug usa fundo escuro e recebe `print`, `Debug.Log` e falhas de
carregamento, `setup()` e `update()`, inclusive do runtime separado. Erros incluem
arquivo/linha e o rastreamento fornecido pelo Lua. A linha de status do editor de
texto só ocupa espaço quando tem uma mensagem (por exemplo, após salvar).
