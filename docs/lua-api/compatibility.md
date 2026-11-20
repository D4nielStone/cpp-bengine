# Lua: compatibilidade e limites

## API `bubble` legada

Os nomes abaixo coexistem com a API em estilo `GameObject`:

| Símbolo | Parâmetros/retorno | Significado |
|---|---|---|
| `bubble.meuID` | inteiro | ID da entidade do script. |
| `bubble.log(message)` | string | Envia uma mensagem ao log Lua. |
| `bubble.delta_time` | número | Delta usado na atualização atual. |
| `bubble.input.is_key_down(key)` | boolean | Consulta tecla, equivalente a `Input.GetKey`. |
| `bubble.input.mouse_delta()` | `x, y` | Delta de mouse, equivalente a `Input.GetMouseDelta`. |
| `bubble.input.set_mouse_captured(value)` | nenhum | Equivalente a `Input.SetMouseCaptured`. |
| `bubble.entity.get_position(id)` | `x, y, z` | Posição local da entidade; devolve zeros se ECS/transform não estiver disponível. |
| `bubble.entity.set_visible(id, value)` | nenhum | Altera `Renderer.visible` se a entidade tiver Renderer. |
| `bubble.entity.remove_physics(id)` | nenhum | Remove o componente Rigidbody da entidade. |
| `bubble.transform.get_position()` | `x, y, z` | Posição local do objeto que contém este script; zeros se a transformação não estiver disponível. |
| `bubble.transform.get_rotation()` | `x, y, z` | Rotação local em graus. |
| `bubble.transform.set_rotation(x, y, z)` | nenhum | Define rotação local em graus. |
| `bubble.transform.move_forward(distance)` | nenhum | Move localmente no plano XZ segundo o yaw local. |
| `bubble.transform.move_right(distance)` | nenhum | Move localmente no plano XZ segundo o yaw local. |

`bubble.transform` é ligado à transformação da entidade do script. Já
`GameObject.Find` e `gameObject:GetComponent` aceitam objetos de qualquer
entidade ainda válida no ECS.

## Carregamento de arquivos e exportação

Scripts são lidos através do sistema de recursos do engine. Em builds com
recursos embutidos, `loadfile(path)` lê o conteúdo pelo caminho do recurso e
`dofile(path)` compila e executa esse conteúdo. Esse modo não oferece acesso
geral ao sistema de arquivos do host via bibliotecas Lua `io`/`os`.

Para exportação, mantenha scripts, imagens e outros assets dentro do projeto e
use caminhos relativos. O empacotador não consegue inferir caminhos externos
montados dinamicamente pelo script.

## Limites atuais explícitos

- `Destroy` remove somente um `Rigidbody`; não destrói entidades ou componentes
  arbitrários.
- Não existe binding Lua genérico para criar entidade, adicionar componentes,
  instanciar prefab ou enumerar todas as entidades.
- A [API de HUD](../HUD.md) oferece widgets de layout, texto, botão e imagem, todos com filhos.
  Novos widgets usam posições livres relativas ao pai (`absolute=true`,
  `relative=true`) e tamanho inicial 160 × 40. Para criar em fluxo, defina
  `absolute=false`; para tamanho natural, use dimensões zero. Cenas antigas
  sem esse campo preservam a referência ao viewport. O fluxo BGUI e
  `SetScreenPosition` mantêm os contratos anteriores. `Find(id)` e `Get(id)`
  retornam nil quando ausentes. Fragmentos personalizados exigem OpenGL 3.3.
- `SoundPlayer` oferece Play/Pause/Resume/Stop e isPlaying. Não oferece seek
  nem sobreposição de vozes no mesmo componente; erros de dispositivo/arquivo
  são registrados pelo backend.
- Os componentes `Text`, `Image` e `Terrain` não estão entre os nomes aceitos
  por `GetComponent`.
- Os estados Lua abrem `base` e `math`; não presuma outras bibliotecas padrão.

Essas limitações descrevem os bindings atuais e não são promessas sobre versões
futuras.

## Globals e GLSL

Os estados Lua continuam separados. `Globals` compartilha cópias de dados na
mesma cena e é limpo quando o sistema de scripts inicia novamente; não é um save
game. A API GLSL nova mantém os formatos de materiais e aceita cenas antigas sem
`post_processing.uniforms`. O suporte de edição cobre `float/int/bool`,
`vec2/vec3/vec4` e `mat4`; samplers extras e uniform blocks não são vinculados
pela API nova. Veja [contratos completos](globals-and-shaders.md).
