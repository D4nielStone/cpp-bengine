#pragma once
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <deque>
#include <memory>
#include <functional>
#include "commons_namespace.hpp"
#include "entities/entity.hpp"

namespace COMMONS_NS {
    /**
	* @struct ecs
	* @brief gerencia as entities
	* @brief nucleo do system ECS
	*/
	struct ecs : public std::enable_shared_from_this<ecs>
	{
	    ~ecs() {
	        entities.clear();
	        mascaras.clear();
	        live_entities.clear();
	    }

	    std::unordered_set<uint32_t> live_entities;
	    /// Armazena mask da entity associada
	    std::unordered_map<uint32_t, component::mask> mascaras;
	    /// Armazena components da entity associada
	    std::map<uint32_t, std::unordered_map<component::mask, std::shared_ptr<component>>> entities;

	    bool is_alive(const uint32_t id) const noexcept;
	    bool validate_invariants() const noexcept;

	    /* Cria new entity */
	    entity create(const uint32_t id = 0);
	    /* Retorna todos os components da entity */
	    component::mask get_components(const uint32_t& id) const;
	    /* Adiciona um component a uma entity */
	    template <typename T, typename... Args>
	    void add(entity& ent, Args&&... args);

	    /* Remove um component a uma entity */
	    template <typename T>
	    void remove(const uint32_t& ent);

	    void remove(const uint32_t& ent);

	    /* Verifica se uma entity possui um component */
	    template <typename T>
	    bool has(const uint32_t& entity);

	    /* Itera pelas entities que possuem determinados components */
	    template <typename... Components, typename Func>
	    void cada(Func func);

	    /* Obhas um component de uma entity */
	    template <typename T>
	    std::shared_ptr<T> get(const uint32_t& entity);
	};

	/* Definições de templates */

	template<typename T, typename ...Args>
	void ecs::add(entity& ent, Args&&... args) {
		if (!is_alive(ent.id)) {
			return;
		}
		mascaras[ent.id] |= T::mask; // Atualiza a máscara no mapa auxiliar
		ent.mask |= mascaras[ent.id];
		entities[ent.id][T::mask] = std::make_shared<T>(std::forward<Args>(args)...); // Adiciona o component
		entities[ent.id][T::mask]->my_object = ent.id;
		try {
			entities[ent.id][T::mask]->reg = shared_from_this();
		} catch (const std::bad_weak_ptr&) {
			entities[ent.id][T::mask]->reg.reset();
		}
	}

	template<typename T>
	inline void ecs::remove(const uint32_t& ent)
	{
		auto it = entities.find(ent);
		if(it == entities.end())
		return;

		it->second.erase(T::mask);

		auto mask = mascaras.find(ent);
		if(mask == mascaras.end())
		return;

		mask->second &= ~T::mask; // Remove o bit correspondente ao component.
	}

	template<typename T>
	inline bool ecs::has(const uint32_t& entity)
	{
		if (!is_alive(entity))
			return false;
		if (mascaras.find(entity) != mascaras.end())
			return (mascaras[entity] & T::mask) != 0;
		else
			return false;
	}

	template<typename ...comps, typename Func>
	inline void ecs::cada(Func func)
	{
		for (const auto& [entity, components] : entities) {
			const auto mask_it = mascaras.find(entity);
			if (mask_it != mascaras.end() &&
				(((mask_it->second & comps::mask) != component::COMPONENTE_NONE) && ...)) {
				func(entity);
			}
		}
	}

	template<typename T>
	inline std::shared_ptr<T> ecs::get(const uint32_t& entity)
	{
		if (!is_alive(entity))
			return nullptr;
		if (entities.find(entity) != entities.end())
		{
			auto it = entities[entity].find(T::mask);
			if(it != entities[entity].end())
			return std::static_pointer_cast<T>(it->second);
			else
				return nullptr;
		}
		else
			return nullptr;
	}
}
