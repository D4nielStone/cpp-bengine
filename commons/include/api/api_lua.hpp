/**
 *  @file api_lua.hpp
 *  Gerencia a configuração e ecs da API do motor com Lua via LuaBridge.
 *
 *  Este file é responsável por expor components internos do motor gráfico ao ambient
 *  de scripts Lua utilizando a biblioteca LuaBridge. Ele define a estrutura `entity`, que
 *  agrega múltiplos components possíveis de uma entity de jogo (transformação, física, câmera,
 *  text, image, renderer e luz direcional), e oferece funções auxiliares para registrar
 *  utilitários, física, time e entradas no estado da máquina virtual Lua.
 *
 *  Ao incluir este cabeçalho e chamar as funções apropriadas de definição, o motor pode ser
 *  facilmente estendido e controlado por meio de scripts Lua, facilitando a criação de lógicas
 *  personalizadas no jogo em time de execução.
 *
 *  Componentes expostos:
 *  - Transformação (posição, rotação, scale)
 *  - Física (integração com Bullet Physics)
 *  - Câmera (visão da cena)
 *  - Texto e image (elements de interface)
 *  - Luz direcional e renderer (system gráfico)
 *
 *  ### Exemplo de uso no script lua:
 *  \code {lua}
 *  local Teste
 *
 *  -- Função default de inicialização
 *  function setup()
 *      Teste = entity(1) -- Primeira entity (possui transformação)
 *      Teste:transform.position.x = 0 -- Altera o component.
 *  end
 *  \endcode
 */
#pragma once
#include <cstdint>
#include <bullet/btBulletDynamicsCommon.h>
#include <sol/sol.hpp>
#include "commons_namespace.hpp"

namespace COMMONS_NS {
    namespace api {
	     /**  Define as classes da api */
	  	void setClasses(sol::state&);
         /**  Define os namespaces da api como math e inputs */
	  	void setNamespaces(sol::state&);
   } /// < namespace api
} /// < namespace commons
