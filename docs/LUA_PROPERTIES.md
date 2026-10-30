# Propriedades Lua no Inspector

Declare propriedades editáveis usando um comentário `--@property` por linha,
seguido de JSON com `name`, `type` e `default`. O editor lê essas declarações
sem executar o script. Exemplo:

```lua
--@property {"name":"speed","type":"number","default":5}
--@property {"name":"enabled","type":"boolean","default":true}
--@property {"name":"title","type":"text","default":"Player"}
--@property {"name":"tint","type":"color","default":[1,0.5,0,1]}
--@property {"name":"icon","type":"image","default":"assets/icon.png"}

function setup()
    print(properties.title, properties.speed)
    print(properties.tint.r, properties.tint.g, properties.tint.b, properties.tint.a)
    print(properties.icon)
end

function update()
    if properties.enabled then
        transform:Translate(Vector3(0, 0, properties.speed * Time.deltaTime))
    end
end
```

- `number`: número finito; `boolean`: caixa de seleção; `text`: texto.
- `color`: seletor RGBA, quatro canais entre 0 e 1; no Lua, um `Color` da API.
- `image`: referência de arquivo, editável pelo caminho ou arrastando uma imagem
  para o campo. Não carrega uma textura automaticamente. O caminho é salvo
  relativo ao projeto quando possível e resolvido para absoluto na execução.
  Uma string vazia representa nenhuma imagem. Movimentações pelo editor atualizam
  as referências da cena; os valores padrão escritos no Lua não são reescritos.

Os nomes devem ser identificadores ASCII (`speed`, `player_name`), únicos dentro
do script. Funções e variáveis locais comuns continuam privadas ao script.

Cada entidade e cada script anexo possuem seus próprios valores, salvos na cena.
Alterações pelo Inspector participam de desfazer/refazer e não modificam o arquivo
Lua. Os valores estão disponíveis em `properties` desde o início da execução do
arquivo, antes de `setup()`. Alterar a tabela pelo Lua não grava a cena.
As propriedades são configuradas no modo de edição, com a entidade desbloqueada.

Ao reconstruir o Inspector ou iniciar a execução, as declarações são relidas.
Nome e tipo iguais preservam o valor da entidade; propriedades novas ou com tipo
alterado usam o novo padrão; declarações removidas deixam de ser expostas.
Renomear uma propriedade equivale a remover a anterior e adicionar uma nova.
Para atualizar imediatamente após editar o arquivo, selecione outra entidade e
volte. Metadados inválidos mostram um erro com arquivo e linha, preservando os
valores anteriores. Na execução, o erro impede a inicialização desse script.
Cenas e scripts antigos sem declarações continuam compatíveis.
