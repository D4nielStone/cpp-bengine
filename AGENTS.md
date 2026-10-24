# Bubble Engine (cpp-bengine): instruções para agentes

## Projeto
ECS, runtime separado, editor com docking e Inspector, serialização de projetos,
scripts Lua, integração com a BGUI e um exemplo FPP.

## Regras gerais
1. O diagnóstico abaixo veio de leitura estática. NENHUM item foi reproduzido.
   Confirme cada problema no código atual (leitura + teste que falha) antes de corrigir.
   Se não existir ou estiver errado, registre e siga adiante. Não invente correções.
2. Mudanças mínimas e focadas. Não refatore fora do escopo da fase pedida.
3. Toda correção vem com teste quando viável. Isole o que depende de OpenGL
   para os testes rodarem sem GPU.
4. Preserve a API pública. Se precisar quebrá-la, justifique e atualize todos os usos
   (engine, editor, runtime, testes, exemplo FPP).
5. Não afirme que algo funciona sem ter compilado e rodado os testes. Se não puder
   validar (sem display/GPU), diga exatamente o que ficou sem validação.
6. Ambiguidade ou decisão de design relevante: pare e pergunte.
7. Proibido nesta etapa: reescrita completa do ECS, mudança da arquitetura de
   renderização, multithreading, micro-otimizações de FPS.
8. Uma fase por vez. Ao terminar: build limpo, testes, commit(s) claros e
   atualização de `docs/STABILIZATION.md` (tabela de status abaixo).
9. Descubra os comandos de build/teste lendo o CMake e o workflow de CI; não os assuma.

## Arquivos-chave
- apps/editor/project_persistence_system.cpp
- commons/include/core/ecs.hpp
- commons/include/components/camera.hpp
- CMakeLists.txt, commons/CMakeLists.txt, tests/CMakeLists.txt
- .github/workflows/build_and_test.yml

## Diagnóstico (hipóteses a confirmar)
| ID | Prio | Hipótese |
|----|------|----------|
| P0-a | P0 | Projeto aberto com `std::ios::trunc` antes de a gravação terminar: falha pode destruir o arquivo anterior |
| P0-b | P0 | `m_entity_names->clear()` antes de o carregamento concluir: falha perde os nomes |
| P0-c | P0 | Carregamento substitui diretamente os mapas do ECS ativo |
| P0-d | P0 | `entities` e `mascaras` podem dessincronizar (`remove<T>()` deixa máscara vazia em `mascaras`) |
| P0-e | P0 | `create()` checa colisão de ID só em `entities`, sem estrutura central de entidades vivas |
| P0-f | P0 | `add<T>()` usa `shared_from_this()`: `std::bad_weak_ptr` se o ECS não estiver em `shared_ptr` |
| P1-a | P1 | README foi atualizado para refletir a exigência real do projeto: C++20, alinhado com `commons` e com o CMake (`cxx_std_20`). |
| P1-b | P1 | CI passa `-DBUILD_RUNTIME=ON`, que o CMake raiz não usa |
| P1-c | P1 | `tests/CMakeLists.txt` usa `GTest::gtest_main` sem declarar a origem |
| P1-d | P1 | `COMMONS_ASSET_DIR` é caminho absoluto do código-fonte (não portável) |
| P1-e | P1 | `active_camera->m_transform = nullptr` no load: risco de uso antes da reinicialização; ciclo de vida dos framebuffers da câmera |
| P2-a | P2 | Serialização sem registro extensível (sem ID estável, versão ou migração) |

## Relatório ao fim de cada fase
Para cada item: (a) CONFIRMADO / NÃO CONFIRMADO / PARCIAL com evidência
(arquivo/linha ou teste); (b) o que mudou; (c) testes adicionados; (d) o que NÃO foi
validado e por quê; (e) riscos e decisões pendentes para revisão.