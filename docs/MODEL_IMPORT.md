# Importação de modelos 3D

O editor importa modelos por meio do Assimp e guarda os resultados e recursos
associados dentro do projeto. Arraste um modelo suportado da área **Arquivos**
para a cena ou use a ação de importação/reimportação no fluxo do asset.

## Opções

O diálogo de importação apresenta prévia orbital e permite configurar:

- importação de outra cópia;
- filtragem de textura: original, bilinear ou trilinear;
- inversão de UVs;
- geração de normais suaves quando ausentes;
- união de vértices idênticos;
- escala uniforme positiva;
- exclusão de nós e meshes selecionados;
- importação de skeleton, skins/pesos e animações.

As opções ficam ao lado do asset em um arquivo de configurações de importação
versionado. Ao reimportar, verifique a seleção e dependências do modelo antes
de substituir os recursos anteriores.

## Dependências e portabilidade

Arquivos laterais de materiais, texturas, buffers e configurações são
considerados dependências do asset. Mantenha-os no projeto e use referências
relativas para projetos portáveis e exportações. O importador trata dependências
de modelo e não deve apagar arquivos de origem externos.

## Limites

Câmeras, luzes e morph targets do arquivo de origem não são convertidos em
entidades do engine pelo diálogo atual. Formatos aceitos dependem do Assimp e
do conteúdo/recursos suportados pelo carregador do engine. Uma prévia que abre
não garante que todas as semânticas do formato original sejam preservadas.

## Rig e pose

O painel **Malhas → Rig e pose** permite editar a pose dos nós e ossos de um
modelo importado sem mover a entidade. Essa pose é animável pelo componente
Animation e por clipes `.banim`; alterações não reescrevem o GLB/FBX original.
Os identificadores de nós seguem caminhos da hierarquia de importação, por
isso reestruturar o arquivo de origem pode exigir revisar animações existentes.

Para criar e editar hierarquia de ossos e pesos, use um modelo nativo `.bmodel`.
A versão 2 desse formato inclui o rig e pesos normalizados de até oito
influências por vértice; a vinculação automática do editor usa até quatro.
Os arquivos sem rig continuam sendo gravados na versão 1. Confira o fluxo em
[Animação de ossos e rigs próprios](ANIMATION.md#animação-de-ossos-e-rigs-próprios).

## Extrusão, Loop Cut e Extract

Em modelos `.bmodel`, selecione faces e pressione **E** para extrudar movendo o
mouse na prévia. O botão **Extrudar face** inicia a mesma operação usando a
**Distância** como deslocamento inicial. Clique para confirmar; **Esc** ou botão
direito cancela e restaura a geometria. A confirmação cria uma única entrada no
histórico, e as faces extrudadas continuam selecionadas.

**Ctrl+R** ou **Loop Cut** permite escolher uma aresta nos modos **Objeto**,
**Faces** e **Arestas**, sem trocar o modo atual. Clique na aresta e mova o mouse
para deslizar o corte; clique novamente para confirmar, use o botão direito para
centralizar ou **Esc** para cancelar. O corte requer um caminho contínuo de
quadriláteros. Pelo botão, uma única aresta já selecionada inicia o slide
diretamente.

**Extract (Ctrl+E)** exporta a seleção para um único `.bmodel` em
`assets/models/<origem>_parts`, preservando as malhas, materiais, grupos de faces
e rig. Use **Selecionar malha** na lista e **Shift** para adicionar outras malhas.
Em modo Objeto, o objeto selecionado inclui todas as suas malhas; em modo de
edição, a seleção de elementos identifica a malha ativa quando não há uma
seleção explícita de malhas. O diálogo informa a quantidade selecionada e
permite nomear o arquivo. A extração preserva o modelo original e recusa
sobrescrever um arquivo existente.
