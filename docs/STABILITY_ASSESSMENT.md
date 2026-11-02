# Avaliação de estabilidade

Este documento define como avaliar estabilidade do Bubble Engine; não é uma
certificação de release nem uma afirmação de que todas as plataformas foram
validadas.

## Superfícies críticas

1. **Persistência**: abertura, gravação, migração, cena principal e caminhos de
   assets. Falhas devem preservar arquivos existentes e ser reportadas.
2. **Ciclo de vida**: entidades/componentes removidos, relações de transform,
   scripts Lua falhos e liberação de recursos OpenGL com contexto ativo.
3. **Runtime/editor**: Play/Stop, fechamento de janelas, foco/entrada, resize
   de framebuffer e execução com assets ausentes.
4. **Distribuição**: execução do pacote fora do checkout e de diretório de
   trabalho diferente, com dependências e assets incluídos.
5. **Compatibilidade**: projetos antigos, formatos futuros, diferentes GPUs,
   drivers, sistemas operacionais e escalas de UI.

## Evidência e estado

Registre para cada validação a revisão do código, configuração, plataforma,
comando, resultado e limitações. Testes automatizados não substituem a
verificação visual de OpenGL nem testes de pacotes relocáveis.

O conjunto automatizado é amplo, mas a cobertura isolada de uma plataforma
não atesta as demais. Não use pontuação numérica de prontidão sem um método,
escopo e dados reproduzíveis associados.

## Critérios antes de uma release

- Build limpo com as configurações suportadas.
- Testes CTest relevantes executados; falhas e skips explicados.
- Fluxo de criar, salvar, reabrir, Play/Stop e exportar testado.
- Nenhuma perda de projeto causada por entrada malformada ou falha conhecida
  de gravação.
- Pacote iniciado fora do checkout e sem depender dos assets de desenvolvimento.
- Revisão dos limites conhecidos e documentação da versão distribuída.

Consulte [Estabilização e validação](./STABILIZATION.md) para os comandos de
validação mantidos pelo projeto.
