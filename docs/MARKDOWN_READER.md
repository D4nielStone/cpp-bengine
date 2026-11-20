# Leitor Markdown do editor

O painel de arquivos e a referência da API compartilham o mesmo leitor.
Os documentos permanecem somente para leitura.

## Imagens

```markdown
![Descrição da imagem](images/preview.png)
![Arquivo com espaços](<images/minha imagem.png>)
```

Imagens locais usam cantos arredondados e reduzem proporcionalmente quando o
painel fica menor. O caminho é relativo à pasta do documento. Imagens ausentes
ou inválidas mostram a descrição e o caminho sem interromper a leitura.
Imagens remotas e HTML embutido não são carregados.

## Tabelas

```markdown
| Campo | Tipo | Valor |
| :--- | :---: | ---: |
| Nome | string | `player` |
| Guia | link | [Abrir](guide.md) |
```

Os dois pontos controlam o alinhamento. Use `\|` para uma barra literal na célula;
blocos de código em linha também podem conter barras. As colunas compartilham
a largura disponível e o texto pode quebrar em várias linhas.

## Código e citações

Código em linha usa crases: `entity:get_position()`.
Blocos delimitados por três ou mais crases ou tils preservam o conteúdo literal,
usam fundo próprio e exibem o nome da linguagem quando informado. Linhas longas
quebram visualmente para caber no painel.

> Uma citação começa com `>` e recebe um fundo de destaque.

## Hiperlinks e referências

```markdown
[Site](https://example.com)
<https://example.com>
[Outro documento](guide.md)
[Arquivo com espaços](guide%20one.md)
[Guia][manual]
[manual][]
[manual]

[manual]: guide.md "Título opcional"
```

Links HTTP, HTTPS e `mailto:` abrem no aplicativo padrão, sem passar por um
interpretador de comandos. Links locais abrem documentos no editor; na
referência da API, navegam entre os documentos do catálogo. Links por referência
podem ser definidos antes ou depois do uso. Também funcionam em tabelas.

Fragmentos em links para outros arquivos abrem o documento no início. A navegação
para âncoras dentro da mesma página ainda não é implementada. Protocolos fora de
HTTP, HTTPS e `mailto:` não são executados.
