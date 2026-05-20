#pragma once
#ifndef EVENT_BUS_H
#define EVENT_BUS_H

// Lightweight typed event bus.
//
// Usage:
//   EventBus bus;
//   auto token = bus.subscribe<CollisionEvent>([](const CollisionEvent& e){ ... });
//   bus.publish(CollisionEvent{entityA, entityB});
//   bus.unsubscribe(token);
//
// Events are structs (any POD or non-POD).
// Subscriptions are invoked immediately in publish() (synchronous).
// Thread-safety: NOT thread-safe by default — use one bus per thread or add a mutex.

#include "string_id.h"
#include "callback.h"
#include "../math/vector.h"
#include <cstdint>
#include <cstring>

// Each event type gets a compile-time ID via template instantiation
template<typename E>
struct EventTypeID {
    static uint32_t value() {
        static uint32_t id = nextID();
        return id;
    }
private:
    static uint32_t nextID() {
        static uint32_t counter = 100u; // start above reserved SID range
        return counter++;
    }
};

using SubToken = uint64_t; // unique subscription handle

class EventBus {
public:
    // Subscribe — returns a token to unsubscribe later
    template<typename E>
    SubToken subscribe(EvtFn handler) {
        uint32_t typeId = EventTypeID<E>::value();
        SubToken tok = makeToken(typeId);
        Channel* ch = getOrCreate(typeId);
        Handler h; h.token = tok; h.fn = rt_move(handler);
        ch->handlers.push_back(rt_move(h));
        return tok;
    }

    // Convenience overload: subscribe with a typed lambda (wraps it in EvtFn)
    template<typename E, typename F>
    SubToken subscribe(F&& f) {
        return subscribe<E>(EvtFn([fn=rt_move(f)](const void* e){
            fn(*static_cast<const E*>(e));
        }));
    }

    // Unsubscribe by token
    void unsubscribe(SubToken token) {
        uint32_t typeId = (uint32_t)(token >> 32);
        for (auto& ch : channels_) {
            if (ch.typeId != typeId) continue;
            auto& v = ch.handlers;
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i].token == token) { v.erase(v.begin()+i); return; }
            }
        }
    }

    // Publish — synchronously calls all subscribers (not re-entrant)
    template<typename E>
    void publish(const E& event) {
        uint32_t typeId = EventTypeID<E>::value();
        for (auto& ch : channels_) {
            if (ch.typeId != typeId) continue;
            size_t count = ch.handlers.size();
            for (size_t i = 0; i < count; ++i) ch.handlers[i].fn(&event);
            return;
        }
    }

    // Deferred publish — queues event, flushed by flushDeferred()
    template<typename E>
    void publishDeferred(E event) {
        uint32_t typeId = EventTypeID<E>::value();
        DeferredEvent de;
        de.typeId = typeId;
        de.data.resize(sizeof(E));
        memcpy(de.data.data(), &event, sizeof(E));
        de.dispatch = EvtFn([this, typeId](const void* ptr){
            for (auto& ch : channels_) {
                if (ch.typeId != typeId) continue;
                size_t count = ch.handlers.size();
                for (size_t i = 0; i < count; ++i) ch.handlers[i].fn(ptr);
            }
        });
        deferred_.push_back(rt_move(de));
    }

    void flushDeferred() {
        // Swap-clear so dispatch can safely queue new deferred events
        PodVector<DeferredEvent> q;
        for (size_t i = 0; i < deferred_.size(); ++i)
            q.push_back(rt_move(deferred_[i]));
        deferred_.clear();
        for (size_t i = 0; i < q.size(); ++i) q[i].dispatch(q[i].data.data());
    }

    void clear() { channels_.clear(); deferred_.clear(); }

private:
    struct Handler { SubToken token; EvtFn fn; };
    struct Channel { uint32_t typeId; PodVector<Handler> handlers; };
    struct DeferredEvent {
        uint32_t          typeId = 0;
        PodVector<uint8_t> data;
        EvtFn             dispatch;
    };

    Channel* getOrCreate(uint32_t typeId) {
        for (auto& ch : channels_) if (ch.typeId == typeId) return &ch;
        Channel c; c.typeId = typeId;
        channels_.push_back(rt_move(c));
        return &channels_.back();
    }

    SubToken makeToken(uint32_t typeId) {
        static uint32_t seq = 0;
        return ((uint64_t)typeId << 32) | ++seq;
    }

    PodVector<Channel>       channels_;
    PodVector<DeferredEvent> deferred_;
};

// Global singleton bus — use for cross-system events
inline EventBus& globalBus() { static EventBus bus; return bus; }

#endif
