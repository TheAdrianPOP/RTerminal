#pragma once
#ifndef EVENT_BUS_H
#define EVENT_BUS_H

// Lightweight typed event bus.
// Supports reentrant subscriptions, unsubscriptions, and nested publishing.

#include "string_id.h"
#include "callback.h"
#include "vector.h"
#include "internal.h"

// Each event type gets a unique runtime ID via template instantiation
template<typename E>
struct EventTypeID {
    static uint32_t value() {
        static uint32_t id = nextID();
        return id;
    }
private:
    static uint32_t nextID() {
        static uint32_t counter = 100u;
        return counter++;
    }
};

using SubToken = uint64_t; // unique subscription handle

class EventBus {
public:
    template<typename E>
    SubToken subscribe(EvtFn handler) {
        uint32_t typeId = EventTypeID<E>::value();
        SubToken tok    = makeToken(typeId);
        Channel* ch     = getOrCreate(typeId);
        Handler  h;
        h.token  = tok;
        h.fn     = rt_move(handler);
        h.active = true;

        if (dispatch_depth_ > 0) {
            pending_subscribes_.push_back({typeId, rt_move(h)});
        } else {
            ch->handlers.push_back(rt_move(h));
        }
        return tok;
    }

    template<typename E, typename F>
    SubToken subscribe(F&& f) {
        return subscribe<E>(EvtFn([fn = rt_forward<F>(f)](const void* e) {
            fn(*static_cast<const E*>(e));
        }));
    }

    void unsubscribe(SubToken token) {
        uint32_t typeId = static_cast<uint32_t>(token >> 32);
        for (auto& ch : channels_) {
            if (ch.typeId != typeId) continue;
            for (size_t i = 0; i < ch.handlers.size(); ++i) {
                if (ch.handlers[i].token == token) {
                    ch.handlers[i].active = false;
                    if (dispatch_depth_ == 0) {
                        ch.handlers.erase(ch.handlers.begin() + i);
                    }
                    return;
                }
            }
        }
        // Also check pending subscriptions if not yet flushed
        for (size_t i = 0; i < pending_subscribes_.size(); ++i) {
            if (pending_subscribes_[i].handler.token == token) {
                pending_subscribes_.erase(pending_subscribes_.begin() + i);
                return;
            }
        }
    }

    template<typename E>
    void publish(const E& event) {
        uint32_t typeId = EventTypeID<E>::value();
        Channel* ch     = findChannel(typeId);
        if (!ch) return;

        ++dispatch_depth_;
        size_t count = ch->handlers.size();
        for (size_t i = 0; i < count; ++i) {
            if (ch->handlers[i].active && ch->handlers[i].fn) {
                ch->handlers[i].fn(&event);
            }
        }
        --dispatch_depth_;

        if (dispatch_depth_ == 0) {
            flushPending();
        }
    }

    template<typename E>
    void publishDeferred(E event) {
        uint32_t typeId = EventTypeID<E>::value();
        DeferredEvent de;
        de.typeId = typeId;
        de.data.resize(sizeof(E));
        memcpy(de.data.data(), &event, sizeof(E));
        de.dispatch = EvtFn([this, typeId](const void* ptr) {
            Channel* ch = findChannel(typeId);
            if (!ch) return;

            ++dispatch_depth_;
            size_t count = ch->handlers.size();
            for (size_t i = 0; i < count; ++i) {
                if (ch->handlers[i].active && ch->handlers[i].fn) {
                    ch->handlers[i].fn(ptr);
                }
            }
            --dispatch_depth_;

            if (dispatch_depth_ == 0) {
                flushPending();
            }
        });
        deferred_.push_back(rt_move(de));
    }

    void flushDeferred() {
        PodVector<DeferredEvent> q;
        for (size_t i = 0; i < deferred_.size(); ++i) {
            q.push_back(rt_move(deferred_[i]));
        }
        deferred_.clear();

        for (size_t i = 0; i < q.size(); ++i) {
            q[i].dispatch(q[i].data.data());
        }
    }

    void clear() {
        channels_.clear();
        deferred_.clear();
        pending_subscribes_.clear();
        dispatch_depth_ = 0;
    }

private:
    struct Handler {
        SubToken token  = 0;
        EvtFn    fn;
        bool     active = true;
    };

    struct Channel {
        uint32_t           typeId = 0;
        PodVector<Handler> handlers;
    };

    struct DeferredEvent {
        uint32_t           typeId = 0;
        PodVector<uint8_t> data;
        EvtFn              dispatch;
    };

    struct PendingSubscribe {
        uint32_t typeId;
        Handler  handler;
    };

    Channel* findChannel(uint32_t typeId) {
        for (auto& ch : channels_) {
            if (ch.typeId == typeId) return &ch;
        }
        return nullptr;
    }

    Channel* getOrCreate(uint32_t typeId) {
        if (Channel* ch = findChannel(typeId)) return ch;
        Channel c;
        c.typeId = typeId;
        channels_.push_back(rt_move(c));
        return &channels_.back();
    }

    SubToken makeToken(uint32_t typeId) {
        static uint32_t seq = 0;
        return (static_cast<uint64_t>(typeId) << 32) | ++seq;
    }

    void flushPending() {
        // Merge pending subscriptions
        for (size_t i = 0; i < pending_subscribes_.size(); ++i) {
            Channel* ch = getOrCreate(pending_subscribes_[i].typeId);
            ch->handlers.push_back(rt_move(pending_subscribes_[i].handler));
        }
        pending_subscribes_.clear();

        // Compact dead handlers
        for (auto& ch : channels_) {
            for (size_t i = 0; i < ch.handlers.size(); ) {
                if (!ch.handlers[i].active) {
                    ch.handlers.erase(ch.handlers.begin() + i);
                } else {
                    ++i;
                }
            }
        }
    }

    PodVector<Channel>          channels_;
    PodVector<DeferredEvent>    deferred_;
    PodVector<PendingSubscribe> pending_subscribes_;
    int                         dispatch_depth_ = 0;
};

// Global singleton bus
inline EventBus& globalBus() { static EventBus bus; return bus; }

#endif // EVENT_BUS_H