# API Lua — referência completa

Os scripts são componentes `code` associados a entidades da cena. Cada
instância de script recebe um ambiente Lua e os objetos referentes à entidade
que a contém. A implementação de referência está em
`commons/src/api/lua.cpp` e `commons/src/components/code.cpp`.

## Guias

1. [Primeiros passos, ciclo de vida e exemplos](./getting-started.md)
2. [GameObject, Transform, Vector3 e funções globais](./objects-and-transforms.md)
3. [Renderer, Rigidbody, Camera, luzes, áudio e animação](./components.md)
4. [Input, Time, Debug e saída de console](./input-time-debug.md)
5. [Propriedades Lua no Inspector](./script-properties.md)
6. [Compatibilidade, carregamento de recursos e limites](./compatibility.md)
7. [HUD e textos que acompanham pontos da cena](../HUD.md)

## Índice rápido da API

| Categoria | Símbolos |
|---|---|
| Valores | `Vector3`, `Color` |
| Entidades | `gameObject`, `GameObject.Find`, `GetComponent`, `SetParent`, `IsCollidingWith`, `IsOverlappingTrigger`, `Destroy` |
| Componentes | `Transform`, `Renderer`, `Rigidbody`, `Camera`, `DirectionalLight`, `PointLight`, `SpotLight`, `SoundListener`, `SoundPlayer`, `HUD`, `Animation`, `Pathfinder` |
| Entrada e tempo | `Input.GetKey`, `Input.GetAxis`, `Input.GetMouseDelta`, `Input.SetMouseCaptured`, `Time.deltaTime`, `Time.time` |
| Cenas | `SceneManager.LoadScene`, `ReloadScene`, `GetActiveScene`, `IsLoading`, `GetLastError` |
| Diagnóstico | `Debug.Log`, `print` |
| Compatibilidade | `bubble.log`, `bubble.input`, `bubble.entity`, `bubble.transform`, `bubble.meuID`, `bubble.delta_time` |

## O que “completa” significa nesta referência

As páginas enumeram os símbolos registrados pelo runtime Lua atual e descrevem
seus tipos, leitura/escrita e limites. Não são expostos por Lua construtores
genéricos de entidades/componentes, nem operações de
reprodução/controle de áudio. A lista não presume APIs comuns em outras engines.

- [Gerenciamento de cenas e Pathfinder para inimigos](./scenes-and-navigation.md)
