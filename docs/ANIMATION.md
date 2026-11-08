# Animation: componente, clipe e gravação automática

A animação de propriedades segue o fluxo **entidade → componente Animation →
Animation Clip**, inspirado no trabalho com clipes da Unity. A cena guarda a
referência ao arquivo; as curvas ficam em um asset `.banim` reutilizável.

## Componente Animation

No Inspector, use **Add Component → Animation**. A seção Animation permite
atribuir um clipe do projeto, editar seu caminho, configurar **Play Automatically**
e a velocidade de reprodução, ou abrir o painel de animação. Um componente sem
clipe é válido e não reproduz nada.

- `clip`: caminho para um arquivo `.banim`, normalmente em `assets/animations/`.
- `autoplay`: inicia a reprodução ao executar o jogo.
- `speed`: multiplicador de tempo de 0 a 100; zero congela o tempo de reprodução.

A duração, as curvas e a opção Loop pertencem ao arquivo de clipe, não à entidade.
Duas entidades que referenciam o mesmo arquivo compartilham suas curvas, mas o
runtime mantém um relógio de reprodução independente para cada componente.

## Criar ou atribuir um arquivo

Abra **File → Animação** ou **Editar animação / criar clipe** no Inspector.
Selecione a entidade na Hierarquia. Digite um nome e use **Criar clipe**; o editor
cria `assets/animations/<nome>.banim` e o atribui ao componente. Não é permitido
sobrescrever um arquivo existente nessa operação. Com um clipe já atribuído,
**Duplicar clipe** cria outro arquivo com as curvas atuais e o atribui à entidade.

Também é possível digitar um caminho relativo e usar **Atribuir .banim**, ou
escolher um dos arquivos existentes pelo seletor do Inspector. Os arquivos devem
estar dentro do projeto. Referências ausentes, malformadas ou que escapam do
projeto são rejeitadas sem substituir o componente atual.

## Gravar alterações automaticamente

Não existe seletor de componente/propriedade nem comando de captura manual.

1. Atribua ou crie o arquivo de clipe.
2. Posicione o cursor pelo campo **Tempo (s)** ou clique na régua.
3. Ative **Gravar** e edite os valores normalmente no Inspector ou pelas
   ferramentas da cena. O editor detecta os canais alterados e cria as trilhas
   e chaves correspondentes no tempo atual.
4. Mova o cursor e faça outra alteração. A régua aplica uma prévia das curvas;
   essa avaliação não gera chaves adicionais. Alterações repetidas no mesmo
   instante atualizam a chave existente.
5. Use **Parar gravação** ao terminar. Fora desse modo, alterações normais da
   cena não modificam o clipe.

Ao criar uma trilha após o tempo zero, o editor guarda também o valor anterior
em uma chave inicial em 0 s. Propriedades booleanas e inteiras são gravadas com
interpolação constante. Apenas os canais que realmente mudaram recebem chaves;
os demais componentes não precisam ser escolhidos ou configurados previamente.

As trilhas existentes aparecem abaixo da régua. É possível editar Tempo e Valor
(confirmando com Enter), mudar Linear/Constante, excluir chaves/trilhas e usar
**Ir** para visualizar um instante. Tempos duplicados, números não finitos e
valores fora dos limites são rejeitados. Duração menor que a última chave também
é rejeitada, sem truncar as curvas.

Mudanças de seleção, troca de clipe e execução do jogo interrompem a gravação.
Desfazer/refazer e a própria prévia não são interpretados como novas alterações
a gravar. Quando uma edição do Inspector e sua captura automática acontecem no
mesmo frame, o histórico agrupa as duas operações para desfazê-las juntas.

Use **Reproduzir prévia** para assistir ao clipe na cena sem executar o jogo.
A reprodução começa no cursor atual, respeita **Loop** e **Velocidade** e desativa
a gravação automática. **Pausar prévia** mantém o instante atual; **Parar prévia**
volta ao tempo zero e aplica a pose inicial. Clicar na régua também pausa.
Ao chegar ao fim de um clipe sem Loop, a prévia pausa na última pose.

A prévia modifica os valores da cena e participa do histórico. **Ctrl+Z** restaura
os valores anteriores à reprodução com uma única operação, sem uma entrada por
frame. Retomar após uma pausa inicia outra operação. Parar e aplicar a pose
inicial também é uma operação separada. A prévia não altera as curvas do clipe.
Trocar de entidade ou clipe, remover o componente Animation, bloquear a entidade,
fechar o painel, executar o jogo ou editar/desfazer/refazer interrompe a reprodução.
A pose visualizada permanece na cena e será incluída se a cena for salva.
A prévia avalia apenas as propriedades animadas; não executa scripts ou simulação
física do jogo.

## Salvar, compartilhar e recarregar

**Salvar clipe** grava apenas o asset. **Save Project** grava os clipes pendentes
e a cena com suas referências. Assets editados continuam sob responsabilidade
do editor mesmo após excluir uma entidade ou atribuir outro clipe, evitando
perder alterações sem aviso. Excluir um componente ou desfazer sua atribuição
não apaga o arquivo de animação.

Cada arquivo usa a transação recuperável já existente no projeto. Uma alteração
externa no arquivo bloqueia sua sobrescrita automática. **Recarregar** lê a versão
do disco e substitui as curvas locais em uma operação que pode ser desfeita;
Ctrl+Z restaura a edição anterior. A versão do disco também é mantida como base
para detectar novos conflitos.

Os clipes são assets compartilhados: salvar um clipe altera a animação de todas
as entidades/cenas que o utilizam. O salvamento de múltiplos assets é feito por
arquivo antes da publicação da cena; não é uma única transação global para todo
o projeto. Cada arquivo individual permanece completo e recuperável.

## Canais disponíveis e extensões

Os adaptadores expõem valores numéricos e booleanos de transformações, câmera,
visibilidade/sombras do renderer, luzes, física, áudio, propriedades públicas Lua,
HUD, texto e propriedades de entidade. A gravação detecta automaticamente esses
canais. Ponteiros, buffers de GPU, referências de entidades e operações
estruturais como carregar modelos, scripts e terreno não são curvas numéricas.

Elementos de HUD precisam de um `id` estável; os caminhos usam esse identificador,
e não a posição na lista. Propriedades Lua usam o arquivo do script e o nome da
propriedade. Alvos removidos ou renomeados ficam indisponíveis, mas suas trilhas
permanecem no arquivo e podem ser editadas/excluídas. O runtime ignora canais
indisponíveis e nunca recarrega componentes inteiros por frame.

Componentes personalizados podem expor seus próprios canais sobrescrevendo os
métodos opcionais da base `commons::component`:

```cpp
std::vector<commons::animation_property> animation_properties() const override {
    return {{"speed", 0.0, 100.0, false}};
}
std::optional<double> read_animation_property(const std::string& path) const override {
    return path == "speed" ? std::optional<double>(speed) : std::nullopt;
}
bool write_animation_property(const std::string& path, double value) override {
    if (path != "speed" || !std::isfinite(value) || value < 0 || value > 100)
        return false;
    speed = value;
    return true;
}
```

O exemplo pressupõe um membro `double speed`. Os canais devem ter nomes únicos
e estáveis. O setter deve validar valores e preservar os invariantes do componente.
O registro/persistência do componente personalizado continua sendo necessário.

## Formatos e compatibilidade

Um componente salvo na cena contém apenas referência e opções de reprodução:

```json
"animation": {
  "clip": "assets/animations/Walk.banim",
  "autoplay": true,
  "speed": 1.0
}
```

O arquivo de clipe tem formato próprio e versão explícita:

```json
{
  "format": "bubble-animation-clip",
  "version": 1,
  "duration": 2.0,
  "loop": true,
  "tracks": [
    {"component": 8, "property": "position.x", "stepped": false,
     "keys": [[0.0, 0.0], [2.0, 3.0]]}
  ]
}
```

`commons::animation_clip` contém as curvas. `commons::animation` referencia o
asset. `commons::animation_system` mantém os relógios por componente e resolve
entidades/componentes vivos a cada amostra. Os arquivos são carregados ao
atribuir/carregar o projeto, não a cada frame.

Animações antigas com curvas embutidas na cena continuam sendo carregadas. No
próximo salvamento, a persistência cria arquivos `legacy-<entidade>.banim` sem
sobrescrever assets existentes e troca os dados embutidos por referências.
Copiar/colar preserva a referência ao clipe, e exportar o jogo inclui os arquivos
`.banim` com caminhos portáveis, inclusive no executável com recursos embutidos.

Versões antigas da engine que não conhecem esse componente/formato não devem
ser usadas para editar cenas que o contenham.

## Mover chaves e navegar pelo painel

Cada trilha tem uma régua própria. Arraste um losango horizontalmente e solte
para mudar seu tempo; a alteração entra uma única vez no histórico. Os valores
são preservados, as chaves continuam ordenadas e um tempo já ocupado é rejeitado.
Também é possível editar o campo **Tempo** numericamente. Arrastar interrompe a
prévia e a gravação automática; não grava mudanças de propriedades.

Layouts roláveis, incluindo o painel de animação em larguras pequenas, oferecem
barra horizontal, gesto horizontal do touchpad e **Shift + roda do mouse**.
A roda sem Shift continua movendo o conteúdo verticalmente. Painéis internos
consomem a rolagem primeiro e a repassam ao pai quando atingem o limite.

## Controle Lua e blending

Use `gameObject:GetComponent("Animation")` para obter o componente existente.
Os comandos retornam `true` quando aceitos e `false` para componente removido,
valores inválidos ou clipes indisponíveis. São processados, em ordem, na próxima
atualização de `animation_system`. Não alteram as curvas nem a referência salva
do componente. Os clipes de destino são carregados ao solicitar Play/CrossFade,
nunca por frame. Os caminhos são relativos ao projeto; caminhos absolutos,
saídas da pasta e arquivos inválidos são rejeitados.

```lua
local animation
function setup()
    animation = gameObject:GetComponent("Animation")
    if animation then animation:Play() end
end

function change_animation()
    -- Crie os clipes no editor antes de usar este caminho.
    if animation then
        animation:CrossFade("assets/animations/Walk.banim", 0.3)
    end
end
```

| Método/propriedade | Comportamento |
|---|---|
| `Play(clip?)` | Retoma; com caminho, inicia esse clipe em zero. No fim de um clipe, reinicia. |
| `Pause()` | Mantém a pose e interrompe o relógio. |
| `Stop()` | Interrompe e aplica a pose de zero do clipe ativo. |
| `Seek(seconds)` | Aplica o instante, limitado à duração, mesmo pausado; cancela o blending. |
| `CrossFade(clip, seconds)` | Faz transição para um clipe, começando em zero. Duração zero troca imediatamente. |
| `SetSpeed(value)` / `speed` | Multiplicador de 0 a 100; zero congela reprodução e transição. |
| `time` | Instante da última pose avaliada, somente leitura. |
| `isPlaying`, `isBlending` | Estado da última atualização, somente leitura. |

CrossFade mistura os canais numéricos dos dois clipes enquanto seus relógios
avançam. Canais constantes, booleanos e inteiros mudam na metade da transição.
Canais ausentes em um dos clipes usam a pose capturada no início como referência.
Uma nova transição durante outra parte da pose visível, evitando saltos para o
clipe anterior. Componentes removidos/substituídos são ignorados com segurança.
Ao terminar, só o clipe de destino continua sendo avaliado. O tempo de transição
é afetado por `speed`. Cada componente mantém reprodução independente.

A referência **API Reference** da barra de menu indexa os arquivos Markdown em
`docs/`, agrupa-os por pasta e pesquisa seus nomes e conteúdos.
