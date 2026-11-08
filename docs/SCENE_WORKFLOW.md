# Gestão visual de cenas e scripts

Abra **Gestão de cenas** pela barra lateral. Esse espaço contém a lista de cenas
existente e três painéis adicionais, que podem ser redimensionados, destacados
ou fechados pelo dock. **Mostrar painéis visuais**, na lista de cenas, reabre os
painéis fechados.

## Conexões entre cenas

O painel apresenta as cenas do projeto em um plano cartesiano: X cresce para a
direita e Y para cima. Arraste um nó para reposicioná-lo e arraste o fundo para
deslocar a vista. Os botões **+**, **−**, **Enquadrar** e **Origem (0, 0)** controlam
a visualização. As posições ficam em `.editor-scene-graph.json`, sem alterar as
cenas ou seus scripts.

As setas representam chamadas `SceneManager.LoadScene("caminho.scene")` nos
scripts associados às entidades de cada cena. `ReloadScene()` produz uma
conexão para a própria cena. Comentários e strings que apenas contêm exemplos
de chamadas não geram conexões. Chamadas com expressões ou destinos ausentes
aparecem como pendências nos detalhes do nó; nenhum script é executado para
resolver esses destinos.

Selecione uma cena para consultar seus scripts e suas conexões. Clicar em um
script abre sua edição visual. **Abrir cena** usa a navegação normal do editor,
incluindo as proteções de alterações pendentes. Esse comando pode também mudar
a cena inicial do projeto, conforme o gerenciador de cenas existente.

**Atualizar grafo** relê as cenas e os documentos abertos. Scripts com alterações
pendentes nos editores são considerados. Após criar/mover arquivos por outros
programas, use **Atualizar scripts e conexões**. Cenas isoladas continuam visíveis.
O grafo é uma referência estática das chamadas Lua; não prevê a ordem, as
condições ou a frequência de execução das transições, nem resolve aliases de
`SceneManager`, funções indiretas ou módulos carregados dinamicamente.

## Scripts do projeto

O painel lista recursivamente os arquivos `.lua` do projeto, inclusive scripts
que ainda não foram associados a entidades. O filtro procura pelo caminho ou
nome. Pastas ocultas e links simbólicos não são percorridos, e referências a
arquivos externos ao projeto não são abertas pelo fluxo visual.

## Script em blocos

Selecionar um script lê seu Lua e apresenta uma sequência de blocos coloridos:
funções/eventos, condições `if`/`elseif`/`else`, repetições `for`, `while` e
`repeat`/`until`, variáveis, atribuições e comandos. Campos editáveis controlam
nomes, argumentos, valores, condições e limites. Expressões desses campos
continuam usando a sintaxe Lua.

A paleta usa um seletor de categoria: **Objeto**, **Propriedades**, **Debug**,
**Scenes**, **Time**, **Física**, **Câmera**, **Luzes**, **Navegação**, **Animação**,
**Interface**, **Entrada**, **Matemática** e **Compatibilidade**. Role a paleta
para consultar os comandos da categoria ou use **Buscar comando na categoria**. Os modelos cobrem os métodos e
propriedades registrados na API atual, incluindo áudio e componentes visuais
em Propriedades; valores somente de leitura oferecem apenas blocos de leitura.
Estruturas de controle e Lua livre continuam disponíveis abaixo da paleta.

**Mover** representa `gameObject.transform:Translate(Vector3(x, y, z))`:
o alvo é editável e X/Y/Z aparecem lado a lado. Vetores e cores literais
oferecem campos separados; expressões como `direcao * Time.deltaTime`
permanecem em um campo, sem perda de código. Chamadas com retorno têm um campo
Resultado. Argumentos de texto usam aspas Lua; referências a componentes exigem
que o componente exista no objeto de destino. Propriedades do Inspector podem
ser lidas e alteradas por `properties.nome`; as declarações `--@property`
continuam preservadas como Lua.

- Clique em um bloco ou em **Encaixar aqui** e escolha um item da paleta para
  inserir dentro de uma estrutura. **Nível principal** seleciona a raiz.
- Arraste um comando da paleta até um cabeçalho ou encaixe para criar o bloco;
  clicar no comando insere no destino selecionado. Durante o arrasto, uma
  prévia flutua sobre os painéis com nome, valores e balanço amortecido de
  pêndulo. Soltar fora dos encaixes cancela a inserção ou movimentação.
- Arraste a alça **::** para outro cabeçalho para inserir antes dele, ou solte em
  um encaixe para inserir no final de um corpo. O movimento inclui os blocos
  aninhados; encaixes cíclicos são rejeitados.
- **Trocar cena** insere uma chamada `SceneManager.LoadScene`; preencha o caminho
  relativo ao projeto em seus argumentos. Ao editar essa chamada, o grafo pode
  ser atualizado a partir do mesmo documento.
- **Excluir bloco**, **Desfazer** e **Refazer** atuam sobre o documento visual.
  Depois de uma edição externa no editor de texto, o visual é relido e o seu
  histórico local é reiniciado.
- **Salvar Lua** verifica a sintaxe sem executar o script e grava o arquivo Lua.
  Erros de sintaxe mantêm o conteúdo no documento para correção.

### Seleção, recolhimento e atalhos

Clique na alça para selecionar: um contorno azul destaca o bloco inteiro.
Os botões **− / +** recolhem ou expandem os campos e os blocos internos sem
alterar o Lua. Selecionar e recolher não recria o painel; os campos continuam
nos mesmos widgets e a rolagem é mantida. Edições estruturais preservam a
posição da vista, limitada à altura do conteúdo restante.

Com o foco na árvore de blocos:

| Atalho | Ação |
|---|---|
| Ctrl+Z | Desfazer |
| Ctrl+Shift+Z ou Ctrl+Y | Refazer |
| Ctrl+C / Ctrl+X | Copiar / recortar o bloco com os seus descendentes |
| Ctrl+V | Colar Lua no corpo selecionado ou depois do bloco selecionado |
| Ctrl+D | Duplicar o bloco e seus descendentes |
| Delete | Excluir o bloco selecionado |
| Ctrl+S | Validar e salvar o Lua |
| Esquerda / Direita | Recolher / expandir |
| Espaço | Alternar recolhimento |

A colagem verifica a sintaxe no destino e não altera o documento em caso de
erro. Copiar usa Lua na área de transferência, permitindo trocar trechos com o
editor de texto. Dentro de um campo, copiar, colar, selecionar e Delete editam
o texto do campo. O editor Lua textual oferece Ctrl+A/C/X/V, Delete,
Ctrl+Z, Ctrl+Shift+Z/Ctrl+Y e Ctrl+S; o histórico de texto permanece separado do
histórico estrutural de blocos. Atalhos dos blocos não atuam sobre entidades ou
sobre o histórico da cena e ficam bloqueados por modais e durante Play.

O visual e o editor de texto usam os mesmos buffers e a mesma persistência de
sessão do painel Arquivos. Os campos sincronizam o texto durante a edição, sem
precisar pressionar Enter. Alternar scripts ou espaços de trabalho não exige
salvar imediatamente. A gravação final continua sendo explícita.

Comentários, propriedades `--@property`, strings multilinha, espaços e finais
de linha são preservados quando o arquivo não é editado. Construções que não
podem ser decompostas com segurança aparecem como **Lua preservado**, com um
campo de código, em vez de serem descartadas. Isso inclui certas funções
anônimas, sintaxe compacta e arquivos com erros. O editor é uma representação
conservadora do Lua existente, não um interpretador separado nem um conversor
geral para o formato de projetos do Scratch.

Se o arquivo mudar no disco desde a abertura, o salvamento visual é bloqueado e
a versão pendente permanece no documento. **Reler do disco** solicita confirmação
antes de descartar as alterações dos editores de texto e blocos e adotar a
versão externa. Edição e salvamento de scripts ficam protegidos durante Play.
