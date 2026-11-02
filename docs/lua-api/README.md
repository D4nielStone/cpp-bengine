# API Lua — referência completa

Os scripts são componentes `code` associados a entidades da cena. Cada
instância de script recebe um ambiente Lua e os objetos referentes à entidade
que a contém. A implementação de referência está em
`commons/src/api/lua.cpp` e `commons/src/components/code.cpp`.

## Guias

1. [Primeiros passos, ciclo de vida e exemplos](./getting-started.md)
2. [GameObject, Transform, Vector3 e funções globais](./objects-and-transforms.md)
3. [Renderer, Rigidbody, Camera, luzes e áudio](./components.md)
4. [Input, Time, Debug e saída de console](./input-time-debug.md)
5. [Propriedades Lua no Inspector](./script-properties.md)
6. [Compatibilidade, carregamento de recursos e limites](./compatibility.md)

## Índice rápido da API

| Categoria | Símbolos |
|---|---|
| Valores | `Vector3`, `Color` |
| Entidades | `gameObject`, `GameObject.Find`, `GameObject:GetComponent`, `Destroy` |
| Componentes | `Transform`, `Renderer`, `Rigidbody`, `Camera`, `DirectionalLight`, `PointLight`, `SpotLight`, `SoundListener`, `SoundPlayer` |
| Entrada e tempo | `Input.GetKey`, `Input.GetAxis`, `Input.GetMouseDelta`, `Input.SetMouseCaptured`, `Time.deltaTime`, `Time.time` |
| Diagnóstico | `Debug.Log`, `print` |
| Compatibilidade | `bubble.log`, `bubble.input`, `bubble.entity`, `bubble.transform`, `bubble.meuID`, `bubble.delta_time` |

## O que “completa” significa nesta referência

As páginas enumeram os símbolos registrados pelo runtime Lua atual e descrevem
seus tipos, leitura/escrita e limites. Não são expostos por Lua construtores
genéricos de entidades/componentes, uma API de desenho de HUD, nem operações de
reprodução/controle de áudio. A lista não presume APIs comuns em outras engines.
