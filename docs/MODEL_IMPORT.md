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
