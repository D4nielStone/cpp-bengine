# Avaliação de prontidão para uma primeira versão estável

Data: 2026-10-28. Base: `98414b73` e alterações locais preexistentes no exemplo
FPP, preservadas. Esta etapa avalia e planeja; não altera a implementação.

## Escopo e conclusão

Referência adotada para a estimativa: uma primeira versão de escopo limitado,
com editor e runtime nativos em Linux e Windows, projeto FPP utilizável,
criação/edição/salvamento/reabertura confiáveis e um pacote executável fora do
checkout. Não significa uma engine generalista completa nem uma promessa de
compatibilidade binária permanente. Esse escopo é uma premissa de planejamento,
não uma decisão de produto já aprovada.

O projeto tem um núcleo funcional com regressões automatizadas relevantes.
Ainda não há evidência suficiente para classificá-lo como estável: há retenção
reproduzida do ECS, validação incompleta de invariantes e lacunas de distribuição
e de aceitação do produto. A classificação proposta é **alpha funcional**.

Estimativa de prontidão: **61/100**, com faixa de julgamento **55–70/100**.
É um índice de evidência e risco, não percentual de código pronto, cobertura,
probabilidade de sucesso ou medida estatística. Uma falha crítica bloqueia a
publicação independentemente da soma.

| Dimensão | Peso | Nota de prontidão | Contribuição | Fundamentação |
|---|---:|---:|---:|---|
| Integridade de projeto/cena | 25% | 80 | 20 | Preparação isolada, transação e recuperação com testes; limites de durabilidade e de exceções ainda existem. |
| Ciclo de vida e integridade do ECS | 20% | 45 | 9 | Entidades vivas centralizadas, mas ciclo de ownership reproduzido e validador incompleto. |
| Fluxo de editor e runtime | 20% | 75 | 15 | Edição, desfazer, Lua, física e renderização têm regressões; falta aceitação interativa completa e prolongada. |
| Distribuição e portabilidade | 20% | 40 | 8 | Ferramentas de build existem; opções não governam alvos e pacote nativo de CI publica somente executáveis, sem prova de relocação. |
| Verificação e manutenção | 15% | 60 | 9 | Suíte automatizada ampla; faltam evidências desta auditoria em Windows, sanitizadores e aceitação de release; documentação contém estados históricos conflitantes. |
| **Total** | **100%** | | **61** | `25×0,80 + 20×0,45 + 20×0,75 + 20×0,40 + 15×0,60`. |

## Evidências e achados novos

| ID | Prioridade proposta | Estado e reprodução | Consequência / próximo passo |
|---|---|---|---|
| A1 | P1, bloqueia release | CONFIRMADO: `component::reg` é `shared_ptr<ecs>` (`commons/include/components/component.hpp:52`), e `ecs::add` guarda `shared_from_this()` (`commons/include/core/ecs.hpp:73`). Teste cria ECS, guarda `weak_ptr`, cria entidade e sai do escopo; `expired()` continua falso. | Ciclo ECS → componente → ECS retém o registro. Não se mediu crescimento por hora nem vazamento total do editor. Corrigir ownership/desligamento sem quebrar a API pública silenciosamente e verificar destruição antes de encerrar OpenGL. |
| A2 | P1 | CONFIRMADO: adicionar `COMPONENTE_CAM` à máscara de entidade que só tem Transform ainda faz `validate_invariants()` retornar `true` (`commons/src/core/ecs.cpp:14–34`). | O verificador não compara a máscara com os componentes reais. A manipulação direta dos mapas públicos foi intencional no teste; não prova que o fluxo normal de UI produza essa corrupção. |
| A3 | P1 | CONFIRMADO: configurar com `BUILD_EDITOR=OFF`, `BUILD_RUNTIME=OFF`, `BUILD_TESTING=OFF` e listar alvos ainda revela `editor_app` e `runtime_app`. As opções existem na raiz, mas `apps/CMakeLists.txt:15–20` cria ambos incondicionalmente. | O contrato de configuração não é cumprido; até build só de runtime passa pela configuração do editor. O script de plataforma limita o alvo compilado, mas não resolve o contrato. |
| A4 | P1 | PARCIAL: assets absolutos por padrão em `commons/CMakeLists.txt`; opção relativa já existe e `scripts/platform_build.sh` copia assets. Workflow nativo publica apenas executáveis. | Não afirmar que toda exportação está quebrada. Validar pacote real fora do checkout, sem assets da fonte e com diretório de trabalho diferente. Incluir bibliotecas dinâmicas exigidas. |
| A5 | P2, corrigir antes da release | PARCIAL: `get_repository_root()` não retorna valor (`apps/editor/systems/project_persistence_system.cpp:499`). | Defeito estático e aviso de compilação; não foi reproduzida uma chamada em execução. Confirmar contrato e usos antes de implementar. |
| A6 | P1 de validação | CONFIRMADO por configuração: em Makefiles, o helper não define `CMAKE_BUILD_TYPE`; o cache desta auditoria ficou vazio. `--config Release` não seleciona otimização nesse gerador. | O build Linux validado não equivale a uma validação Release otimizada. Adicionar configuração explícita e executar a suíte nesse modo. |

Os dois testes C++ dirigidos foram criados fora do repositório em
`/tmp/bengine-audit-probes.cpp`. Ambos falharam como esperado; código de saída 1,
log `/tmp/bengine-audit-probes.log`. Devem entrar na suíte como regressões na
fase de correção, acompanhados do conserto. Não se adicionou teste vermelho ao
build normal. A reprodução foi repetida contra as bibliotecas do build limpo desta auditoria,
com as mesmas duas falhas; log `/tmp/bengine-audit-probes-clean.log`.

Reprodução independente de A3:

```sh
cmake -S . -B /tmp/bengine-audit-options-20261028 \
  -DBUILD_TESTING=OFF -DBUILD_EDITOR=OFF -DBUILD_RUNTIME=OFF
cmake --build /tmp/bengine-audit-options-20261028 --target help
```

## Releitura do diagnóstico original

| Item | Estado atual nesta avaliação | Evidência / limite |
|---|---|---|
| P0-a | NÃO CONFIRMADO como truncamento direto atual; correção transacional anterior presente | `project_file_transaction.hpp`; suíte de falha de temporários, rollback e interrupção. Não há `fsync`/`FlushFileBuffers`; não prometer resistência a queda de energia. |
| P0-b | NÃO CONFIRMADO no fluxo atual coberto | Nomes preparados e aplicados após staging; testes de falha preservam nomes. |
| P0-c | NÃO CONFIRMADO como substituição antecipada nos cenários cobertos | ECS temporário, aplicação após parsing; testes preservam registro em falhas. Falha de alocação na aplicação final não foi simulada. |
| P0-d | NÃO CONFIRMADO como defeito atual | Remover último componente preserva entidade viva e entrada de máscara zero por contrato. Existe regressão; A2 é outro problema. |
| P0-e | NÃO CONFIRMADO no código atual | `create()` consulta `live_entities`; isso não elimina todos os riscos de alteração direta dos mapas públicos. |
| P0-f | NÃO CONFIRMADO como `bad_weak_ptr` atual | Exceção é tratada; ECS sem `shared_ptr` fica sem vínculo `reg`. Isso não prova equivalência funcional dos dois modos nem resolve A1. |
| P1-a | NÃO CONFIRMADO como desalinhamento atual | README e CMake indicam C++20. |
| P1-b | PARCIAL | Opção declarada, mas não consumida para criação dos alvos: A3 corrige a conclusão excessiva da tabela histórica. |
| P1-c | NÃO CONFIRMADO no código atual | GoogleTest declarado por FetchContent. Download em build limpo continua dependendo de rede/cache. |
| P1-d | PARCIAL | Caminho absoluto de desenvolvimento e modo relativo de exportação coexistem; falta aceitação de distribuição: A4. |
| P1-e | PARCIAL | Resize adiado e regressões OpenGL existem; cenário original de ponteiro nulo não foi reproduzido. Destruição de recursos/contexto exige auditoria junto de A1. |
| P2-a | PARCIAL | Documentos já têm `version: 1` e componentes têm nomes textuais como `transform`. Falta registro extensível e política de evolução/migração; não falta toda identificação/versionamento. |

Esses estados descrevem a árvore atual. Não invalidam reproduções históricas
registradas em `STABILIZATION.md` nem constituem uma prova de ausência de bugs.

## Estratégia sequencial

Estimativa: **21–34 dias úteis de esforço**, aproximadamente **4–7 semanas**
para uma pessoa familiarizada com o código, dedicada ao projeto, com máquinas
Linux/Windows e ambiente gráfico disponíveis. É previsão de planejamento,
sem medição de velocidade da equipe; problemas novos, indisponibilidade de
ambientes e mudanças de escopo podem ampliar o prazo.

| Fase | Esforço | Trabalho focado | Critério de saída |
|---|---|---|---|
| 1. Ciclo de vida e invariantes | 4–6 dias | Incorporar reproduções A1/A2; revisar ownership, remoção/troca de cenas e ordem de liberação GPU; exercitar criação/destruição, exceções de componentes e limites da API. Resolver A5 após confirmar seu contrato. | Regressões passam; ECS expira quando deve; recursos liberados com contexto válido; ASan/UBSan e verificação de leaks sem achados atribuíveis aos fluxos cobertos. |
| 2. Build e pacote reproduzível | 4–7 dias | Corrigir A3/A6; testar combinações ON/OFF; definir resolução de assets; produzir arquivos de distribuição com assets e dependências; ajustar instruções de instalação ao que realmente é entregue. | Build limpo e testes em Release; pacote Linux/Windows inicia e abre FPP fora da fonte, incluindo execução a partir de outro diretório. |
| 3. Aceitação de ponta a ponta | 5–8 dias | Criar projeto, importar modelo, editar componentes/script, desfazer/refazer, salvar/reabrir, Play/Stop e executar pacote. Exercitar arquivo inválido, asset ausente, pasta sem permissão, resize e DPI. Sessão FPP prolongada com monitoramento de memória. | Roteiro registrado nos dois sistemas; nenhum crash, perda de dados ou bloqueio de fluxo; gráficos testados sem skips no ambiente dedicado. |
| 4. Contratos e robustez de persistência | 5–8 dias | Documentar versão 1 e compatibilidade; testar rejeição de versão futura e leitura de fixtures antigas. Ampliar falhas de I/O e relações inválidas apenas onde houver reprodução. Decidir necessidade de durabilidade contra queda de energia. | Contrato documentado, regressões estáveis, falhas preservam o estado anterior dentro da garantia definida. Registro genérico de serializers só se necessário ao escopo. |
| 5. Candidata e publicação | 3–5 dias | Consolidar CI e relatório atual; disponibilizar candidata para uso real, corrigir bloqueadores reproduzidos, gerar pacote e notas/limitações conhecidas. | Zero P0/P1 de release abertos, checks abaixo completos e tag sobre a revisão exata testada. |

Cada fase termina com build novo, testes pertinentes, commit(s) focados e
atualização de `STABILIZATION.md`, incluindo falhas, skips e limites. Nenhuma
fase autoriza reescrita do ECS, nova arquitetura de renderização,
multithreading ou otimizações especulativas. Antes de implementação, confirmar
as decisões de produto/ownership que afetem API ou garantias de persistência.

## Critérios de publicação

- Editor e runtime compilam em Linux e Windows na configuração publicada.
- Suíte passa; contagem de testes ignorados é explícita. Job gráfico dedicado
  executa efetivamente as regressões OpenGL.
- ASan/UBSan e análise de leaks cobrem criação, carga, troca, Play/Stop e saída.
- Falhas de save/load preservam arquivos e estado conforme garantia documentada.
- Pacote contém assets/dependências e executa sem checkout disponível.
- FPP completa o roteiro de movimento, câmera, colisão e coleta; editor completa
  criação/edição/desfazer/salvar/reabrir/exportar nas plataformas suportadas.
- Formato de projeto e limites de compatibilidade estão documentados.
- Zero defeitos conhecidos que causem crash, perda de dados ou bloqueiem esses fluxos.

## Validação desta avaliação

Build limpo de editor/runtime concluído; **111/111 entradas CTest passaram**,
sem skips encontrados no log detalhado, incluindo testes OpenGL. Os dois testes
adicionais de diagnóstico falham separadamente, confirmando A1/A2; não integram
essas 111 entradas. Comando derivado do workflow e de `scripts/build.sh`:

```sh
bash scripts/build.sh /tmp/bengine-stability-audit-20261028 --test
```

Logs: `/tmp/bengine-stability-audit-20261028.log` e
`/tmp/bengine-stability-audit-20261028/Testing/Temporary/LastTest.log`. O cache
selecionou Lua 5.4 neste host; isso não valida a combinação Lua 5.3 indicada no
README/CI Linux. A configuração de build ficou sem tipo explícito em Makefiles
(A6). O aviso de retorno ausente em `get_repository_root` persiste.

Não foram realizadas correções
nem validação interativa do editor/FPP, execução Windows, instalação/relocação
real, sanitizadores, falha real de disco ou queda de energia. Assets do exemplo
FPP já tinham alterações locais antes desta avaliação e foram preservados.
