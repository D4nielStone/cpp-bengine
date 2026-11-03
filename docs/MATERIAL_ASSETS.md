# Materiais e modelos nativos

`.bmat` é o formato JSON versionado de materiais da engine. Ele usa
`format: "bengine-material"` e `version: 1`, com objetos `uniforms` e `textures`.
Os uniforms preservam valores escalares, booleanos, vetores, cores e matrizes.
Uniforms que apontam para valores são salvos como uma cópia do valor atual;
ponteiros e dados transitórios de instanciamento não são persistidos.

As texturas guardam caminho e filtragem; seus caminhos são relativos à pasta do
material. Texturas embutidas no modelo original são preservadas como pixels RGBA
no próprio `.bmat`. O carregamento dos arquivos não exige um contexto OpenGL;
a criação dos recursos gráficos acontece ao renderizar.

Cada mesh de um `.bmodel` pode ter uma propriedade `material` apontando para um
`.bmat`, relativa à pasta do modelo. Essa propriedade é opcional e mantém a
compatibilidade com os modelos versão 1 existentes. A importação copia o modelo,
os materiais e suas texturas mantendo a estrutura das referências.

Ao salvar um projeto, cada material da cena ganha uma referência `file` ao
`.bmat`. Os valores legados continuam no JSON da cena para leitores antigos;
na versão atual, `file` é a fonte do material. Cenas antigas sem essa propriedade
continuam sendo carregadas. Um material ausente ou inválido interrompe o
carregamento da cena antes de substituir a cena aberta.

Os salvamentos automáticos geram nomes baseados no conteúdo e publicam os
materiais antes de publicar as referências do modelo ou da cena. Nunca substituem
uma versão anterior do material. Uma falha ao salvar pode deixar arquivos sem
referências, mas mantém os materiais usados pelo salvamento anterior. Não há
remoção automática desses arquivos, pois outros modelos ou cenas podem usá-los.

O runtime usa o mesmo carregador do editor, inclusive para recursos embutidos em
um executável exportado. A exportação reescreve as referências necessárias para
que o pacote continue funcionando quando movido para outra pasta.
