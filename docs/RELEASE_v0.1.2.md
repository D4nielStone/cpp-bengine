# Bubble Engine 0.1.2

Esta página registra a linha de versão 0.1.2 documentada pelo código-fonte
atual. Para instruções de build e exportação, veja o [README](../README.md).

## Aplicações

O repositório fornece `editor_app` e `runtime_app`. O editor salva os dados do
projeto/cena, permite selecionar assets e componentes e abre o runtime em uma
janela/processo separado pelo controle Play. O runtime pode carregar um projeto
com `--project`.

## Formatos e recursos

O manifesto de projeto usa o formato 2, com migração de formato 1. Os arquivos
de cena mantêm seu formato/versionamento próprio. Scripts Lua podem declarar
propriedades para o Inspector; câmera, iluminação e Rigidbody possuem bindings
documentados em [API Lua](./lua-api/README.md).

## Limites documentados

O runtime e o painel Game do editor não compartilham a simulação: Play executa
outro processo. A API Lua ainda não oferece um sistema de HUD, criação genérica
de entidades/componentes ou controle de reprodução sonora.

Este arquivo não substitui os checks, artefatos ou notas específicas publicados
com uma distribuição binária.
