# Importação de modelos 3D

O editor usa o Assimp instalado no sistema. GLB, glTF, FBX, OBJ, Blender,
Collada e 3DS são reconhecidos pelo navegador, além dos outros formatos
anunciados pelo Assimp. Reconhecer a extensão não garante que a versão do
arquivo seja suportada pelo decodificador instalado, especialmente `.blend`.

## Uso

- Selecione o modelo no painel de arquivos e clique em **Mostrar importador 3D**.
- Falhas ao copiar um modelo externo, soltar um modelo na cena ou atribuí-lo ao
  Renderer abrem o importador com o diagnóstico.
- Corrija arquivos inválidos ou dependências ausentes e use **Tentar novamente**.
- Ajuste a seleção e clique em **Importar / Reimportar**. Fechar cancela a edição
  das opções sem gravá-las. A importação é bloqueada durante a execução da cena.

## Opções

A hierarquia mostra os nodes e quantas referências a malhas cada um contém.
Desabilitar um node exclui a geometria de toda sua subárvore. A transformação
continua disponível para ossos dependentes: desabilitar geometria não rompe o rig.
As seleções individuais de meshes valem para todas as instâncias daquela mesh.

É possível inverter UVs, gerar normais suaves quando ausentes, unir vértices,
ajustar a escala uniforme, habilitar skeleton, skins/pesos e selecionar os clipes importados.
Desabilitar skeleton mantém as transformações originais dos ossos e desativa
a deformação por pesos; a opção de skins depende de skeleton.
O painel também lista ossos, quantidade de pesos, duração e canais dos clipes,
materiais e texturas embutidas. Nodes sem nome ou com nomes repetidos são
identificados internamente pela posição na árvore, por exemplo `0/2/0`.

As opções são gravadas em `<modelo>.import.json`, com versão de esquema 1,
por substituição de arquivo temporário. O modelo original não é reescrito.
O arquivo de opções acompanha a cópia de dependências para dentro do projeto.
Editor e runtime leem as mesmas opções. Reimportar atualiza os Renderers ativos
que usam o mesmo caminho; o carregador prepara a substituição antes de trocar
as malhas, preservando o modelo anterior quando o novo carregamento falha.

## Filtragem de texturas

No importador, selecione **Original**, **Bilinear** ou **Trilinear** e confirme em
**Importar / Reimportar**. A escolha vale para as texturas do modelo no editor e
no runtime. Bilinear interpola os texels; trilinear também interpola entre níveis
de mipmaps, reduzindo a cintilação de texturas distantes. Isso não substitui
antialiasing das bordas da geometria.

O campo opcional `texture_filter` aceita `original`, `bilinear` ou `trilinear`.
Arquivos antigos sem esse campo preservam a filtragem original. Valores inválidos
são rejeitados. Em C++, `commons::texture::filtering` usa `commons::texture_filter`.
Os parâmetros compartilhados com a BGUI são restaurados após desenhar a malha.

## Reprodução e limites

O carregador mantém a cena do Assimp, aplica as transformações dos nodes e
avalia posição, escala e rotação (interpolação de quaternions). Skins usam os
pesos e as matrizes de offset dos ossos. A avaliação ocorre na CPU, enviando os
vértices ao renderizador existente. Não há novo shader nem mudança de arquitetura.

O primeiro clipe selecionado toca em loop. Em C++, `model::set_animation(index)`
seleciona outro clipe importado pelo índice original; `update_animation(seconds)`
permite avaliar uma pose explicitamente. O desenho usa tempo decorrido desde o
carregamento ou a troca de clipe. Com animações desabilitadas, usa a pose original.

Texturas embutidas comprimidas são decodificadas pela BGUI usando um arquivo
transitório removido após a leitura; texturas embutidas não comprimidas usam seus
pixels RGBA. O suporte a codecs de imagem continua sendo o da BGUI.

Não há conversão de câmeras/luzes em entidades, animação de morph targets,
retargeting, blending de clipes, editor de rig ou edição de keyframes. O importador
indica esses limites. Seleções por índices/posição devem ser revistas se uma nova
versão do arquivo mudar a ordem de nodes, malhas ou clipes. Rigs que dependem de
nomes de ossos duplicados precisam de verificação específica. O custo de skinning
na CPU depende da quantidade de vértices e ossos; não houve benchmark nesta fase.

A janela de importação separa as configurações, à esquerda, da prévia 3D orbital,
à direita. A prévia sombreada enquadra automaticamente a geometria na pose inicial
e acompanha as seleções de nodes e meshes. Arraste sobre a imagem para orbitar,
use a roda do mouse para aproximar ou afastar e “Redefinir órbita” para restaurar
a vista inicial. “Atualizar prévia da seleção” também permite atualizar a imagem
manualmente. A prévia não exibe texturas nem reproduz animações.

Texturas externas são resolvidas primeiro pelo caminho relativo gravado no modelo,
depois a partir da pasta pai e nas pastas `Texture`, `Textures`, `texture` e
`textures`, junto do modelo ou ao lado de `source`. O carregamento usa a mesma
regra após a importação; a cópia preserva a estrutura relativa do pacote.
