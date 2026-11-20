# API Lua

Esta página é o ponto de entrada legado e compatível para a referência Lua.
Consulte o [índice da documentação Lua](./lua-api/README.md) para a API
completa, organizada por tópico.

## Céu padrão

Novas câmeras usam `Color(0.16, 0.16, 0.16, 1)` em `Camera.backgroundColor`,
com zênite também cinza. Cores explícitas de cenas existentes são preservadas.
Os templates Empty/FPP herdam esse padrão; o comando visual de cor da câmera
e a dica da conclusão de código usam o mesmo valor.

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

## HUD

`gameObject:AddHUD()` cria ou recupera a HUD; `GetComponent("HUD")` consulta.
Veja [HUD modular](HUD.md) para hierarquia em qualquer widget, posições relativas,
`HUD:Find(id)`/`Get(id ou índice)`, fontes TTF/OTF, imagens, cores, opacidade
e fragmentos GLSL. `Add(tipo,texto,pai)` aceita índice ou ID do pai; omitir
o pai cria na raiz. `SetScreenPosition` mantém coordenadas do viewport.
Novos widgets usam layout livre relativo ao pai e tamanho 160 × 40; use
`absolute=false` para fluxo BGUI. Cenas antigas preservam seus padrões salvos.
Novos campos de aparência e `id`/`callback` estão documentados no guia.

## Animações e consulta no editor

`GetComponent("Animation")` expõe reprodução, pausa, busca e transições por
`CrossFade`. Veja [controle Lua e blending](./ANIMATION.md#controle-lua-e-blending).
Na barra de menu, **API Reference** indexa recursivamente os arquivos
`.md` e `.markdown` de `docs/`, agrupa os resultados pela pasta e permite
pesquisar tanto nomes quanto conteúdo. Selecione um documento para lê-lo no
editor; novos guias Markdown aparecem no catálogo sem uma lista de tópicos
codificada manualmente.

- [Gerenciamento de cenas e Pathfinder para inimigos](./lua-api/scenes-and-navigation.md)

O modo **Gestão de cenas → Script em blocos** também oferece a API em uma
paleta por categorias, com busca e argumentos editáveis. Por exemplo,
`Translate(Vector3(x, y, z))` aparece como **Mover**, com alvo e três campos.
Veja [edição visual de scripts](./SCENE_WORKFLOW.md#script-em-blocos).

## Variáveis compartilhadas e uniforms GLSL

`Globals.Set/Get/Has/Remove/Clear` compartilham valores entre arquivos Lua da mesma
cena. `Renderer:SetShader/SetUniform/RemoveUniform`, `Camera.postProcessShader`,
`Camera:SetUniform/RemoveUniform` e `Shader.GetUniforms` expõem parâmetros de
materiais e filtros. Veja [contratos, exemplos, persistência e limites](lua-api/globals-and-shaders.md).

O [guia ilustrado de efeitos visuais](lua-api/visual-effects.md) mostra como
configurar god rays, neblina volumétrica, flare radial, GTAO, bloom e pólen
instanciado no Inspector e em Lua.

## Controle de áudio

`SoundPlayer:Play()`, `Pause()`, `Resume()` e `Stop()` solicitam transporte ao
backend espacial existente; `isPlaying` é somente leitura. Consulte o contrato
[de áudio](./lua-api/components.md#áudio) e o [exemplo](./lua-api/examples/spatial_audio.lua).

## HUD: alinhamento de texto

`HudElement.alignment` aplica Start/Center/End também ao texto renderizado,
com os mesmos valores 0/1/2 já disponíveis. Veja [HUD](HUD.md).
