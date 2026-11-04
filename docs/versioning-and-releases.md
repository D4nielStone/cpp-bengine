# Versionamento e migrações

## Versão do engine

A versão do engine é definida pelo `project()` no CMake raiz e disponibilizada
em `BUBBLE_ENGINE_VERSION`. Para builds, use os scripts documentados no
[README](../README.md#building). Uma release deve ser criada a partir de uma
revisão identificável e acompanhada por artefatos e validação do workflow de
release.

## Manifesto de projeto

O campo `format` identifica o tipo de documento como
`bubble-engine-project`. O campo numérico `version` identifica a versão
do formato, atualmente 2. O manifesto também inclui `engine_version`,
`minimum_engine_version` e `project_version`. Projetos com formato 1 recebem a migração registrada no
`project_migration_manager`; versões desconhecidas ou versões mínimas/engine
mais novas que a instalada são rejeitadas.

Quando o editor grava migração, cria um diretório de backup ao lado do projeto
antes de substituir o manifesto. O dado de cena/assets não deve ser reescrito
por essa migração do manifesto.

## Arquivos de cena

O projeto aponta para uma cena, normalmente `.scene`; `.world` também é
reconhecido em fluxos de abertura. A cena tem seu próprio campo `format` e
versão, independente da versão do manifesto. Componentes serializados usam
nomes textuais. Adicionar formato/campo requer validar entrada, compatibilidade
com projetos existentes e testes de ida e volta/migração.

## Política para mudanças

1. Não reutilize número de formato para significado diferente.
2. Rejeite versões futuras explicitamente; não salve dados desconhecidos como
   se fossem válidos.
3. Faça conversões em memória e preserve originais/backup até validação e
   gravação segura.
4. Atualize fixtures, testes de migração e esta documentação junto com a
   implementação.
5. Não confunda `project_version` (versão do projeto/jogo) com versão do
   engine ou versão do formato de arquivo.
