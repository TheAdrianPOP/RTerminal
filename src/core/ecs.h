#pragma once
#ifndef ECS_H
#define ECS_H

#include "internal.h"
#include "engine_config.h"
#include "vector.h"
#include "str.h"
#include "arena.h"
#include "move.h"

template<typename... Ts> struct ecs_first_impl;
template<typename First, typename... Rest>
struct ecs_first_impl<First, Rest...> { using type = First; };
template<typename... Ts>
using ecs_first_t = typename ecs_first_impl<Ts...>::type;

struct EntityID {
    uint32_t value = kEntityNullValue;

    uint32_t index() const { return value & kEntityIndexMask; }
    uint32_t gen()   const { return value >> kEntityGenShift; }

    static EntityID make(uint32_t idx, uint32_t gen) {
        return { ((gen & kEntityGenMask) << kEntityGenShift) | (idx & kEntityIndexMask) };
    }

    bool operator==(EntityID o) const { return value == o.value; }
    bool operator!=(EntityID o) const { return value != o.value; }
    bool valid()               const { return value != kEntityNullValue; }
};

inline constexpr EntityID ECS_NULL_ENTITY = { kEntityNullValue };

using ComponentTypeID = uint32_t;

namespace ecs_detail {
    inline ComponentTypeID& typeCounter() {
        static ComponentTypeID c = 0;
        return c;
    }
}

template<typename T>
inline ComponentTypeID componentTypeID() {
    static ComponentTypeID id = ecs_detail::typeCounter()++;
    return id;
}

template<typename T>
class SparseSet {
public:
    static constexpr uint32_t INVALID = 0xFFFF'FFFFu;

    explicit SparseSet(LinearArena* arena = nullptr)
        : sparse_(arena)
        , dense_data_(arena)
        , dense_ids_(arena)
    {}

    void add(EntityID e, const T& val) {
        ensure_sparse(e.index());
        uint32_t& slot = sparse_[e.index()];
        if (slot != INVALID) {
            dense_data_[slot] = val;
            return;
        }
        slot = static_cast<uint32_t>(dense_data_.size());
        dense_data_.push_back(val);
        dense_ids_.push_back(e);
    }

    void add(EntityID e, T&& val) {
        ensure_sparse(e.index());
        uint32_t& slot = sparse_[e.index()];
        if (slot != INVALID) {
            dense_data_[slot] = rt_move(val);
            return;
        }
        slot = static_cast<uint32_t>(dense_data_.size());
        dense_data_.push_back(rt_move(val));
        dense_ids_.push_back(e);
    }

    void remove(EntityID e) {
        uint32_t idx = e.index();
        if (idx >= sparse_.size() || sparse_[idx] == INVALID) return;

        uint32_t dense_idx = sparse_[idx];
        if (dense_idx >= dense_ids_.size() || dense_ids_[dense_idx] != e) return;

        uint32_t last_idx = static_cast<uint32_t>(dense_data_.size()) - 1;
        if (dense_idx != last_idx) {
            dense_data_[dense_idx] = rt_move(dense_data_[last_idx]);
            dense_ids_[dense_idx]  = dense_ids_[last_idx];
            sparse_[dense_ids_[dense_idx].index()] = dense_idx;
        }

        dense_data_.pop_back();
        dense_ids_.pop_back();
        sparse_[idx] = INVALID;
    }

    bool has(EntityID e) const {
        uint32_t idx = e.index();
        return idx < sparse_.size() && sparse_[idx] != INVALID
            && dense_ids_[sparse_[idx]] == e;
    }

    T& get(EntityID e) {
        assert(has(e) && "SparseSet::get — entity does not have component");
        return dense_data_[sparse_[e.index()]];
    }

    const T& get(EntityID e) const {
        assert(has(e) && "SparseSet::get — entity does not have component");
        return dense_data_[sparse_[e.index()]];
    }

    T*       begin()       { return dense_data_.data(); }
    T*       end()         { return dense_data_.data() + dense_data_.size(); }
    const T* begin() const { return dense_data_.data(); }
    const T* end()   const { return dense_data_.data() + dense_data_.size(); }
    const PodVector<EntityID>& entities() const { return dense_ids_; }
    size_t                     size()     const { return dense_data_.size(); }

private:
    PodVector<uint32_t>  sparse_;
    PodVector<T>         dense_data_;
    PodVector<EntityID>  dense_ids_;

    void ensure_sparse(uint32_t idx) {
        if (idx >= sparse_.size()) {
            sparse_.resize(idx + 1, INVALID);
        }
    }
};

struct ErasedPool {
    virtual ~ErasedPool() = default;
    virtual void                       removeErased(EntityID e) = 0;
    virtual size_t                     size()             const = 0;
    virtual const PodVector<EntityID>& entities()         const = 0;
};

template<typename T>
struct TypedPool : ErasedPool, SparseSet<T> {
    explicit TypedPool(LinearArena* arena = nullptr) : SparseSet<T>(arena) {}

    void                       removeErased(EntityID e)     override { SparseSet<T>::remove(e); }
    size_t                     size()                 const override { return SparseSet<T>::size(); }
    const PodVector<EntityID>& entities()             const override { return SparseSet<T>::entities(); }
};

class EntityManager {
public:
    explicit EntityManager(LinearArena* arena = nullptr)
        : generations_(arena)
        , free_list_(arena)
    {}

    EntityID create() {
        if (!free_list_.empty()) {
            uint32_t idx = free_list_.back();
            free_list_.pop_back();
            generations_[idx] = (generations_[idx] + 1) & kEntityGenMask;
            return EntityID::make(idx, generations_[idx]);
        }
        uint32_t idx = static_cast<uint32_t>(generations_.size());
        generations_.push_back(0);
        return EntityID::make(idx, 0);
    }

    void destroy(EntityID e) {
        assert(alive(e) && "EntityManager::destroy — stale or invalid EntityID");
        free_list_.push_back(e.index());
        generations_[e.index()] = (generations_[e.index()] + 1) & kEntityGenMask;
    }

    bool alive(EntityID e) const {
        uint32_t idx = e.index();
        return idx < generations_.size() && generations_[idx] == e.gen();
    }

    size_t capacity() const { return generations_.size(); }

private:
    PodVector<uint32_t> generations_;
    PodVector<uint32_t> free_list_;
};

class World {
public:
    explicit World(size_t pool_memory_bytes = kDefaultEcsArenaInitialBytes)
        : ecs_arena_(pool_memory_bytes)
        , pools_(&ecs_arena_)
        , em_(&ecs_arena_)
    {}

    EntityID createEntity() { return em_.create(); }

    void destroyEntity(EntityID e) {
        assert(em_.alive(e));
        for (auto& entry : pools_) {
            entry.pool->removeErased(e);
        }
        em_.destroy(e);
    }

    bool alive(EntityID e) const { return em_.alive(e); }

    template<typename T>
    void addComponent(EntityID e, const T& val = T{}) {
        assert(em_.alive(e));
        pool<T>().add(e, val);
    }

    template<typename T>
    void addComponent(EntityID e, T&& val) {
        assert(em_.alive(e));
        pool<T>().add(e, rt_move(val));
    }

    template<typename T>
    void removeComponent(EntityID e) {
        pool<T>().remove(e);
    }

    template<typename T>
    T& get(EntityID e) {
        return pool<T>().get(e);
    }

    template<typename T>
    const T& get(EntityID e) const {
        const auto* p = cpool<T>();
        assert(p && "World::get — component pool does not exist");
        return p->get(e);
    }

    template<typename T>
    bool has(EntityID e) const {
        if (!em_.alive(e)) return false;
        const auto* p = cpool<T>();
        return p ? p->has(e) : false;
    }

    template<typename... Comps>
    bool hasAll(EntityID e) const {
        return (has<Comps>(e) && ...);
    }

    template<typename... Comps, typename Fn>
    void each(Fn&& fn) {
        auto& driving = smallestSparseSet<Comps...>();
        const auto& ent_list = driving.entities();
        size_t n = ent_list.size();
        if (n == 0) return;

        // Local snapshot guarantees reentrancy for nested each() calls
        PodVector<EntityID> snapshot(&ecs_arena_);
        snapshot.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            snapshot.push_back(ent_list[i]);
        }

        for (size_t i = 0; i < snapshot.size(); ++i) {
            EntityID e = snapshot[i];
            if (!em_.alive(e)) continue;
            if (!hasAll<Comps...>(e)) continue;
            fn(e, get<Comps>(e)...);
        }
    }

    template<typename T>
    SparseSet<T>& sparseSet() { return pool<T>(); }

    template<typename T>
    const SparseSet<T>* sparseSet() const { return cpool<T>(); }

private:
    LinearArena ecs_arena_;

    struct PoolEntry {
        ComponentTypeID         id;
        OwnedSingle<ErasedPool> pool;
    };
    PodVector<PoolEntry> pools_;
    EntityManager        em_;

    template<typename T>
    TypedPool<T>& pool() {
        ComponentTypeID id = componentTypeID<T>();
        for (auto& entry : pools_) {
            if (entry.id == id) {
                return *static_cast<TypedPool<T>*>(entry.pool.get());
            }
        }
        pools_.push_back({id, OwnedSingle<ErasedPool>(new TypedPool<T>(&ecs_arena_))});
        return *static_cast<TypedPool<T>*>(pools_.back().pool.get());
    }

    template<typename T>
    const TypedPool<T>* cpool() const {
        ComponentTypeID id = componentTypeID<T>();
        for (const auto& entry : pools_) {
            if (entry.id == id) {
                return static_cast<const TypedPool<T>*>(entry.pool.get());
            }
        }
        return nullptr;
    }

    template<typename... Comps>
    ErasedPool& smallestSparseSet() {
        ComponentTypeID best_id = smallestPool<Comps...>();
        for (auto& entry : pools_) {
            if (entry.id == best_id) return *entry.pool.get();
        }
        assert(false && "smallestSparseSet: pool not found");
        return *pools_[0].pool.get();
    }

    template<typename... Comps>
    ComponentTypeID smallestPool() {
        ComponentTypeID best_id = componentTypeID<ecs_first_t<Comps...>>();
        size_t          best_sz = pool<ecs_first_t<Comps...>>().size();

        auto check = [&]<typename T>() {
            size_t sz = pool<T>().size();
            if (sz < best_sz) {
                best_sz = sz;
                best_id = componentTypeID<T>();
            }
        };
        (check.template operator()<Comps>(), ...);
        return best_id;
    }
};

#endif // ECS_H