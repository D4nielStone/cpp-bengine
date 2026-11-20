# Estufa low-poly: implementação e limites

A engine continua genérica. A cena, a sequência de despertar, os emissores,
a geometria e as animações ficam no projeto do jogo; nenhum ID, nome de flor,
criocâmara ou regra narrativa foi acrescentado ao runtime da engine.

## Atualização de 20 de novembro de 2026

- A câmera do jogo usa GTAO com máscara em ponto flutuante e filtro bilateral.
  O pipeline compartilha a profundidade opaca entre a oclusão, o pólen e o
  pós-processamento. O filtro CRT foi retirado da cadeia da câmera porque
  recortava as bordas da cena. O arquivo original continua no projeto.
- A cadeia da câmera integra neblina volumétrica, raios ocluídos pelo mapa de
  sombras solar, flare radial, bloom, correção de cor e fade da abertura.
  O flare aparece quando o sol se projeta na tela e sua região fica visível.
  O bloom e a composição final usam a saída LDR da câmera.
- A grama tem 128 manchas instanciadas e as duas coleções de samambaias têm 26
  instâncias. O vento deforma os vértices. As samambaias usam a mesma
  deformação na passagem de sombras.
- As 30 entidades de pólen foram consolidadas em 120 billboards instanciados.
  Animação e ciclo de vida ocorrem no shader. O pólen é aditivo, com transição
  junto à geometria opaca e sem gravação de profundidade.
- A captura `captures/greenhouse-vfx-gameplay.png` é do frame 470 da cena
  principal em 960 × 540, com o controlador ativo. O log registrou a
  inicialização do FPP e a abertura da criocâmara com liberação de gás.
  `captures/greenhouse-vfx-interior.png` usa uma câmera de inspeção na cópia do
  projeto. O manifesto e a cena anteriores estão em
  `captures/before-greenhouse-vfx-2ohi844z`.
- O MCP pode ler e editar a cena carregada pelo editor. A configuração da
  porta local e o comportamento de arquivos e histórico estão em `docs/MCP.md`.

Vidro refrativo, condensação, DOF e os efeitos narrativos da lista anterior
continuam pendentes. Os detalhes abaixo registram o estado da primeira
implementação da estufa.

## Recursos reutilizáveis

- `SoundPlayer` oferece `Play`, `Pause`, `Resume`, `Stop` e `isPlaying` em Lua.
  O sistema espacial existente mantém o controle de vozes, atenuação e listener.
  O transporte é transitório, não altera o formato salvo e não se propaga na cópia
  de componentes. Componentes removidos rejeitam comandos. A última solicitação
  feita antes da atualização prevalece. A aceitação do comando não certifica
  decodificação ou disponibilidade de dispositivo.
- `commons/assets/shaders/bloom_grade.frag`: brilho LDR aproximado e color grading
  com parâmetros editáveis pelo fluxo genérico de materiais/câmeras existente.
- `commons/assets/shaders/emissive.frag`: superfície autoiluminada com cor e
  intensidade editáveis. Não produz iluminação indireta nem luzes automaticamente.
- Referência dentro do editor: a documentação Markdown e a conclusão Lua incluem
  o contrato novo; o catálogo visual inclui os quatro controles e leitura de
  `isPlaying`, sem setter para esse estado.

- O MCP permite gravar fontes GLSL `.vert`/`.frag` com os mesmos limites,
  caminhos confinados, verificação de conflito e transações dos demais assets.
  A compilação exige o contexto gráfico do editor/runtime.

## Cena do projeto local

O trabalho partiu da cena salva do último projeto usado pelo editor. Não havia
editor em execução no início desta sessão. O MCP opera arquivos salvos, sem
acesso ao estado não salvo, comandos ou histórico de um editor aberto.

Antes de editar foram copiados manifest, cena e controlador FPP para
`captures/before-cryo-20261117` no projeto. Todos os IDs originais foram mantidos;
o registro da flor (ID 7), incluindo componentes e transformação, foi preservado.
O grupo da estufa foi reposicionado em torno da flor, mantendo os filhos.

Os dois MP3 fornecidos foram recuperados dos anexos e copiados para `assets/audio`.
Pássaros usam um emissor externo em loop com atenuação espacial. A liberação de
gás é um one-shot espacial acionado por Lua no início da abertura. Nenhum áudio
mecânico separado foi fornecido ou sintetizado.

A porta usa um `.banim` separado; a locomoção aguarda quatro segundos. A câmera
do jogador recebe orientação inicial, cores matinais, neblina e o filtro de
brilho/cor. O filtro de câmera portátil anterior é desatribuído, mantendo seu
arquivo no projeto. A geometria acrescenta rochas, câmara, faixas emissivas,
canteiros, folhagem facetada, treliças e plantas pendentes. Pólen e pequenos
vegetais usam clipes reutilizáveis de movimento.

## Prioridades dos efeitos

| Prioridade | Efeito | Estado / próximo recurso genérico necessário |
|---|---|---|
| Alta | Bloom + emissive | Shaders reutilizáveis e aplicados; bloom aproximado em LDR, sem pipeline HDR multipass. |
| Alta | God rays | Pendente: passagem com profundidade/oclusão, posição da luz e buffers próprios. A luz direcional não equivale a raios volumétricos. |
| Alta | Vidro refrativo | Pendente: captura da cena opaca, profundidade, ordenação/transparência e material de transmissão. Os vidros existentes não são refrativos. |
| Alta | Poeira/pólen | Pólen geométrico animado; não há emissor genérico de partículas novo nem simulação volumétrica de poeira. |
| Alta | Vento | Clipes de oscilação em vegetação adicional; a flor original não foi alterada. Deformação por vértice e campo global de vento ficam pendentes. |
| Alta | Color grading | Exposição e tint genéricos; LUT e gerenciamento HDR ficam pendentes. |
| Média | Névoa | Neblina de distância existente configurada para manhã; não volumétrica. |
| Média | Condensação | Pendente: camada parametrizada no material de vidro; depende do fluxo de transmissão. |
| Média | Memory echoes | Pendente: definir comportamento e fornecer composição/animação acionável por scripts; não habilitado no despertar. |
| Média | DOF | Pendente: acesso ao buffer de profundidade e controles de foco/abertura no pós-processamento. |
| Baixa | Distorção da realidade | Pendente; desativada. |
| Baixa | Environment morphing | Pendente; sem alterar dados ou entidades originais. |

A imagem de referência orienta a direção visual (estrutura alta, vegetação densa,
vidros envelhecidos e luz dourada). A equivalência visual depende especialmente
dos recursos de vidro e volumetria ainda pendentes. Não classificar uma captura
produzida ou um processo encerrado com código zero como aprovação visual.

## Evidência visual e limites de validação

A cena final tem 95 entidades (14 originais). Os arquivos de áudio no projeto
foram comparados por SHA-256 com os anexos recuperados. A cena principal continua
selecionada e seu controlador FPP continua atribuído. As cópias de inspeção foram
removidas após capturar as imagens; a cena principal não teve sua câmera trocada
para produzir a vista externa.

Capturas no projeto: `captures/greenhouse-morning-interior.png` e
`captures/greenhouse-mountain-overview.png`. A imagem interna vem de uma cópia de
inspeção com input desativado para manter o enquadramento inicial, após 1300
frames de simulação real; física, animação da porta e script do gás continuaram
ativos. A externa usa uma câmera de inspeção, no frame 1. Ambas foram renderizadas
em 1280×720 e revisadas visualmente. A miniatura do projeto foi atualizada, com
cópia da anterior no backup.

Também houve execução da cena principal com controlador ativo: processo encerrado
com código zero e log de abertura/gás. Não houve erros de áudio ou Lua nesses
logs. Um ensaio anterior com 3600 frames não produziu PNG/relatório e não foi
considerado sucesso de captura. O backend real de áudio passou pelo teste de
transporte; a mixagem e orientação espacial ainda precisam de escuta manual.
A exploração interativa completa e colisões de canteiros/estrutura não foram
certificadas. A geometria decorativa acrescentada não fornece um mundo completo
de colisores fora do piso do cume.

A conexão remota do ChatGPT não foi provisionada. O adaptador HTTP local já
existente foi preservado e testado; a implantação HTTPS/OAuth continua dependendo
de infraestrutura externa, conforme `docs/MCP.md`.


## Resultado dos testes

Configuração limpa e compilação integral concluídas. A suíte final executou 663
casos: 647 passaram, 13 falharam e 3 foram ignorados. As 13 falhas têm os mesmos
nomes da linha de base anterior às alterações desta sessão; nenhuma falha nova
permaneceu. Os cinco testes acrescentados, a conclusão de áudio atualizada, o
catálogo visual e os testes MCP stdio/HTTP passaram. Veja a relação das falhas em
`docs/STABILIZATION.md`. Logs e comparação foram guardados em
`captures/before-cryo-20261117/validation` no projeto.
