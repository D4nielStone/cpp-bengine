# Propriedades Lua no Inspector

Propriedades são declaradas como linhas JSON em comentários `--@property` no
arquivo `.lua`. O editor lê somente esse metadado: não executa o script para
popular o Inspector.

```lua
--@property {"name":"speed","type":"number","default":5}
--@property {"name":"enabled","type":"boolean","default":true}
--@property {"name":"label","type":"text","default":"Player"}
--@property {"name":"tint","type":"color","default":[1,0.5,0,1]}
--@property {"name":"icon","type":"image","default":"assets/icon.png"}
```

Tipos aceitos: `number` finito; `boolean`; `text` string; `image` string de
caminho de asset; `color`, array RGBA de quatro números no intervalo `[0, 1]`.
O nome deve começar com letra ASCII ou `_`, conter somente letras, dígitos e
`_`, e ser único dentro do arquivo.

Os valores ficam no componente de script e são independentes para cada entidade
e script adicional. Ao recarregar, um valor salvo prevalece sobre o default se
nome e tipo forem compatíveis. O global `properties` está disponível antes da
execução do código de topo, `setup` e `update`; cores chegam como `Color` e
imagens relativas são resolvidas pela raiz do projeto.

Erros de JSON, nomes duplicados e defaults inválidos são reportados com o
caminho e a linha do script. A forma completa de uso e os exemplos estão em
[API Lua — propriedades de script](./lua-api/script-properties.md).
