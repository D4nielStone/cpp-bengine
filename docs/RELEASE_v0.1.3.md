# Bubble Engine v0.1.3

- Editor de modelos `.bmodel`, edição de geometria e materiais por malha, navegação e gizmo de transformação.
- Melhorias de layout, escala, atalhos, persistência do workspace e preview do runtime.
- Correção da criação de colisores Bullet no Windows, sem depender de RTTI da biblioteca externa.
- Testes e exportação passam a usar projetos gerados pelos templates Empty/FPP, sem depender de assets de demonstração removidos.
- Correção da medição de menus BGUI e validação da conversão CMYK nativa do Windows. A suíte Windows volta a executar sem exclusões de MenuBarTests e ImageTests.
- Formatos existentes de projetos e cenas preservados; `.bmodel` armazena geometria e referências opcionais a materiais `.bmat`, carregados pelo editor e pelo runtime.
