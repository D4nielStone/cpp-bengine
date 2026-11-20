# Variáveis compartilhadas e shaders GLSL

## Valores entre arquivos Lua

Cada componente de script mantém seu próprio estado Lua. Use `Globals` para
compartilhar dados entre scripts da mesma cena, inclusive em entidades distintas:

```lua
-- produtor.lua
function setup()
    Globals.Set("partida", {pontos = 0, jogadores = {"Ana", "Bia"}})
end

-- consumidor.lua
function update()
    local partida = Globals.Get("partida")
    if partida then
        partida.pontos = partida.pontos + 1
        Globals.Set("partida", partida)
    end
end
```

| Função | Comportamento |
|---|---|
| `Globals.Set(nome, valor)` | Grava uma cópia do valor; `nil` remove a chave. |
| `Globals.Get(nome)` | Retorna uma cópia ou `nil` se ausente. |
| `Globals.Has(nome)` | Consulta a existência. |
| `Globals.Remove(nome)` | Remove e retorna se a chave existia. |
| `Globals.Clear()` | Remove todos os valores compartilhados da cena. |

Nomes precisam ser não vazios e não podem conter NUL. Valores aceitos: booleanos,
números finitos, strings e tabelas aninhadas com chaves string ou inteiras.
Inteiros Lua de 64 bits são preservados. Funções, threads, userdata (incluindo
`GameObject`, `Vector3` e `Color`), tabelas cíclicas e estruturas com mais de 32
níveis ou 10.000 nós são rejeitados sem alterar o valor anterior. Para vetores e
cores, compartilhe tabelas de números; para entidades, compartilhe IDs e resolva
com `GameObject.Find(id)` antes de usar.

As cópias não compartilham referências de tabelas. Regrave com `Set` após editar.
Os dados sobrevivem à remoção do script que os escreveu, mas são transitórios:
um novo início do sistema de scripts limpa o armazenamento e outra cena possui
seu próprio armazenamento. Não são gravados em projetos nem arquivos de save.
A ordem de `setup()` entre scripts não é garantida; inicialize com um responsável
e trate `nil` nos consumidores.

## Uniforms de materiais e pós-processamento

A mesma representação de parâmetros usada nos materiais também atende o filtro
GLSL da câmera. Os métodos copiam valores e verificam se o componente ainda está
anexado à entidade. Índices de malha começam em **1**.

```lua
local renderer = gameObject:GetComponent("Renderer")
renderer:SetShader(1, "", "shaders/material.frag") -- vertex vazio usa pbr.vert
renderer:SetUniform(1, "tint", "vec4", {1, 0.5, 0.25, 1})
renderer:SetUniform(1, "roughness", "float", 0.5)
renderer:RemoveUniform(1, "roughness")

local camera = gameObject:GetComponent("Camera")
camera.postProcessShader = "shaders/post.frag" -- vazio desativa o filtro
camera:SetUniform("exposure", "float", 1.25)
camera:SetUniform("weights[0]", "float", 0.8)
camera:RemoveUniform("weights[0]")
```

| Tipo GLSL | Valor Lua |
|---|---|
| `float` | Número finito representável como float |
| `int` | Inteiro de 32 bits |
| `bool` | Booleano |
| `vec2`, `vec3`, `vec4` | Tabela sequencial com 2, 3 ou 4 números |
| `mat4` | Tabela com 16 números, em ordem de colunas |

`SetUniform` usa o nome GLSL exato, incluindo campos de structs e índices de
arrays (estes começam em **0**). O tipo informado precisa corresponder à declaração
GLSL. Parâmetros com tipo incompatível são ignorados no envio ao GPU; o shader
personalizado conserva seu valor inicial. Não há conversão de strings em números. Uniforms ausentes ou eliminados
pelo compilador não têm efeito. `RemoveUniform` retorna booleano e restaura o
valor inicial do GLSL na próxima renderização do shader personalizado.
Shaders personalizados restauram seus uniforms antes de cada desenho, evitando
que materiais e câmeras usando o mesmo programa compartilhem valores por acidente.

Caminhos são relativos à pasta do projeto; paths absolutos continuam aceitos.
Para exportar, mantenha os arquivos no projeto. `SetShader` com fragment vazio
retorna ao shader padrão; trocar apenas o vertex sem fragment personalizado não
é suportado. Os parâmetros dos materiais mantêm a persistência `.bmat` existente;
os da câmera são gravados em `post_processing.uniforms`. Cenas antigas sem esse
campo continuam válidas. Alterações durante Play seguem o ciclo normal do editor.

## Consultar e editar uniforms expostos

```lua
local uniforms = Shader.GetUniforms("pbr.vert", "shaders/material.frag")
for _, uniform in ipairs(uniforms) do
    Debug.Log(uniform.name .. " : " .. uniform.type .. " [" .. uniform.size .. "]")
end
```

`Shader.GetUniforms(vertex, fragment)` exige contexto gráfico ativo e retorna
uma lista de tabelas `{name, type, size}` dos uniforms ativos e endereçáveis do
programa vinculado. Arrays usam o nome retornado pelo OpenGL (`weights[0]`) e seu
tamanho ativo. Uniform blocks e declarações otimizadas são omitidos. Tipos
reconhecidos: os da tabela acima, `sampler2D` e `samplerCube`; os demais retornam
`unsupported`. Falhas sem programa válido geram erro Lua; uma recompilação
inválida preserva o último programa válido, como no carregador existente.

O Inspector mostra campos dos uniforms numéricos e booleanos ativos quando a
seção é reconstruída com um shader personalizado selecionado. Valores de vetores
e matrizes são separados por vírgulas; campo vazio remove a substituição e usa o
valor inicial do GLSL. As edições têm desfazer/refazer e são salvas junto do
material ou câmera. Uniforms reservados do renderizador continuam sob seu controle.

O catálogo visual contém as operações de `Globals`, consulta de uniforms,
seleção de shaders e edição/remoção de parâmetros de materiais e câmera. A
conclusão de código acompanha esses métodos. Este guia integra a referência
Markdown disponível dentro do editor.

## Contrato do fragment de pós-processamento

O fragment é GLSL 330 core e recebe `in vec2 uv`, `uniform sampler2D sceneColor`
e `uniform vec2 viewportSize`. O motor fornece a imagem da cena e seu tamanho;
esses nomes são reservados. Veja o [fragment de exemplo](examples/exposure.frag)
e o [script controlador](examples/shared_shader.lua).

```glsl
#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D sceneColor;
uniform float exposure = 1.0;
void main() {
    vec4 scene = texture(sceneColor, uv);
    color = vec4(scene.rgb * exposure, scene.a);
}
```

O filtro é uma passagem de tela inteira após a oclusão ambiente. O vertex padrão
de materiais continua fornecendo atributos e uniforms do pipeline PBR; um vertex
customizado precisa respeitar os atributos usados pelas malhas. Matrizes de
câmera/modelo, luzes, neblina, sombras, instâncias e os samplers do motor são
reservados; não os substitua por parâmetros personalizados.

A generalização cobre valores escalares, vetores, cores via `vec4`, matrizes
`mat4`, campos de structs e elementos de arrays desses tipos. A API nova não
carrega texturas adicionais, não expõe UBO/SSBO, compute shaders, `uint`, vetores
inteiros nem outras dimensões de matriz. Samplers consultados são informativos;
texturas de materiais continuam usando o fluxo de materiais existente.
Shaders de câmera recebem a cor do passe anterior, a profundidade opaca e
dados da luz direcional. Shaders de HUD mantêm seu contrato próprio.
[Entradas dos shaders de câmera](visual-effects.md#entradas-para-criar-um-passe-de-câmera)

## Filtros reutilizáveis e emissão

`commons/assets/shaders/bloom_grade.frag` é um filtro genérico opcional com
`bloomStrength`, `bloomThreshold`, `bloomRadius`, `exposure` (float) e `tint`
(vec3). Selecione-o no campo de pós-processamento e edite os uniforms no Inspector
ou por Camera:SetUniform. Copie o arquivo para o projeto para exportação portátil.
É um brilho aproximado de 25 amostras em LDR; não é bloom HDR multipass nem DOF.
Zero em bloomStrength e tint=(1,1,1), exposure=1 preserva a cor de entrada.

`commons/assets/shaders/emissive.frag`, com o vertex PBR padrão, oferece
`emissionColor` (vec3) e `emissionStrength` (float), editáveis nos materiais pelo
mesmo fluxo genérico. A superfície fica autoiluminada; não ilumina outras malhas
nem gera luz indireta. Adicione uma luz explícita se necessário.
Para raios ocluídos, neblina volumétrica e pólen instanciado, consulte o
[guia ilustrado de efeitos visuais](visual-effects.md).

O filtro bloom_grade também oferece `fadeToBlack` (float): 0 mantém a imagem;
1 produz preto. O valor é limitado a [0,1] no shader. Controle-o em Lua via
Camera:SetUniform e combine com texto HUD para aberturas sem efeitos de gravação.
