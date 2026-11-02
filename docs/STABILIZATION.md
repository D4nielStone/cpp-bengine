# Estabilização e validação

Este documento é um guia operacional, não um histórico automático de commits
nem um relatório de testes executados em cada máquina.

## Build e testes

Linux:

```sh
./scripts/build.sh
./scripts/build.sh /tmp/cpp-bengine-build --test
```

Windows:

```powershell
.\scripts\build.ps1
.\scripts\build.ps1 -RunTests
```

Para validar um alvo focado em um build CMake já configurado:

```sh
cmake --build out/build/Debug --target editor_app
ctest --test-dir out/build/Debug --output-on-failure
```

Use `ctest -R <expressão>` para selecionar casos específicos. Informe o comando
exato e o resultado observado; não descreva compilação como teste, nem declare
sucesso para uma execução que não terminou.

## Checklist de mudanças

- Reproduza a falha e identifique os módulos e formatos envolvidos.
- Faça a menor correção completa, sem remover trabalho local não relacionado.
- Adicione regressão para comportamento e para persistência quando aplicável.
- Valide build dos alvos afetados e testes relevantes.
- Revise warnings, recursos GPU e estado de projetos após falhas.
- Atualize a documentação e declare o que ficou sem validação.

## Áreas que pedem validação manual

Renderização e interação dependem de contexto OpenGL, drivers, display, DPI e
entrada nativa. Exportação depende do alvo, bibliotecas e recursos empacotados.
Os testes que pulam por falta de display não contam como confirmação gráfica.

Para uma avaliação de critérios mais ampla, veja
[STABILITY_ASSESSMENT.md](./STABILITY_ASSESSMENT.md).
