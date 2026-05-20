#pragma once
#ifndef CALLBACK_H
#define CALLBACK_H

// VoidFn — minimal std::function<void()> replacement without <functional>.
// Move-only. Small-buffer optimised for closures <= 24 bytes.
// Compatible with non-capturing and capturing lambdas.

#include "move.h"
#include <cstdlib>
#include <cstring>

struct VoidFn {
    static constexpr size_t kSboSize = 24;

    void (*invoke_) (void*)           = nullptr;
    void (*destroy_)(void*)           = nullptr;
    void (*moveto_) (void*, void*)    = nullptr;
    alignas(8) char sbo_[kSboSize]   = {};
    void*            ptr_             = nullptr; // == sbo_ when SBO, else heap

    VoidFn() = default;

    template<typename F>
    VoidFn(F&& f) {
        using Fn = typename RmRef<F>::type;
        invoke_  = [](void* p) { (*static_cast<Fn*>(p))(); };
        if constexpr (!__is_trivially_destructible(Fn))
            destroy_ = [](void* p) { static_cast<Fn*>(p)->~Fn(); };
        moveto_  = [](void* dst, void* src) {
            new (dst) Fn(rt_move(*static_cast<Fn*>(src)));
        };
        if (sizeof(Fn) <= kSboSize) {
            new (sbo_) Fn(rt_move(f));
            ptr_ = sbo_;
        } else {
            ptr_ = malloc(sizeof(Fn));
            new (ptr_) Fn(rt_move(f));
        }
    }

    VoidFn(VoidFn&& o) noexcept
        : invoke_(o.invoke_), destroy_(o.destroy_), moveto_(o.moveto_) {
        if (o.ptr_ == o.sbo_) {
            if (o.moveto_) o.moveto_(sbo_, o.sbo_);
            ptr_ = sbo_;
        } else {
            ptr_ = o.ptr_;
        }
        o.ptr_ = nullptr; o.invoke_ = nullptr;
        o.destroy_ = nullptr; o.moveto_ = nullptr;
    }

    VoidFn& operator=(VoidFn&& o) noexcept {
        if (this != &o) {
            reset();
            invoke_ = o.invoke_; destroy_ = o.destroy_; moveto_ = o.moveto_;
            if (o.ptr_ == o.sbo_) {
                if (o.moveto_) o.moveto_(sbo_, o.sbo_);
                ptr_ = sbo_;
            } else {
                ptr_ = o.ptr_;
            }
            o.ptr_ = nullptr; o.invoke_ = nullptr;
            o.destroy_ = nullptr; o.moveto_ = nullptr;
        }
        return *this;
    }

    VoidFn(const VoidFn&)            = delete;
    VoidFn& operator=(const VoidFn&) = delete;

    ~VoidFn() { reset(); }

    void operator()() const { if (invoke_ && ptr_) invoke_(ptr_); }
    explicit operator bool() const { return invoke_ != nullptr; }

private:
    void reset() noexcept {
        if (ptr_) {
            if (destroy_) destroy_(ptr_);
            if (ptr_ != sbo_) free(ptr_);
        }
        ptr_ = nullptr; invoke_ = nullptr; destroy_ = nullptr; moveto_ = nullptr;
    }
};

// ── EvtFn ─────────────────────────────────────────────────────────────────────
// Like VoidFn but calls with a single `const void*` argument.
// Used by EventBus to store type-erased event handlers.
struct EvtFn {
    static constexpr size_t kSboSize = 24;

    void (*invoke_) (void*, const void*)  = nullptr;
    void (*destroy_)(void*)               = nullptr;
    void (*moveto_) (void*, void*)        = nullptr;
    alignas(8) char sbo_[kSboSize]        = {};
    void*            ptr_                 = nullptr;

    EvtFn() = default;

    template<typename F>
    EvtFn(F&& f) {
        using Fn = typename RmRef<F>::type;
        invoke_  = [](void* p, const void* arg) { (*static_cast<Fn*>(p))(arg); };
        if constexpr (!__is_trivially_destructible(Fn))
            destroy_ = [](void* p) { static_cast<Fn*>(p)->~Fn(); };
        moveto_  = [](void* dst, void* src) {
            new (dst) Fn(rt_move(*static_cast<Fn*>(src)));
        };
        if (sizeof(Fn) <= kSboSize) {
            new (sbo_) Fn(rt_move(f));
            ptr_ = sbo_;
        } else {
            ptr_ = malloc(sizeof(Fn));
            new (ptr_) Fn(rt_move(f));
        }
    }

    EvtFn(EvtFn&& o) noexcept
        : invoke_(o.invoke_), destroy_(o.destroy_), moveto_(o.moveto_) {
        if (o.ptr_ == o.sbo_) {
            if (o.moveto_) o.moveto_(sbo_, o.sbo_);
            ptr_ = sbo_;
        } else { ptr_ = o.ptr_; }
        o.ptr_ = nullptr; o.invoke_ = nullptr; o.destroy_ = nullptr; o.moveto_ = nullptr;
    }

    EvtFn& operator=(EvtFn&& o) noexcept {
        if (this != &o) {
            reset();
            invoke_ = o.invoke_; destroy_ = o.destroy_; moveto_ = o.moveto_;
            if (o.ptr_ == o.sbo_) {
                if (o.moveto_) o.moveto_(sbo_, o.sbo_);
                ptr_ = sbo_;
            } else { ptr_ = o.ptr_; }
            o.ptr_ = nullptr; o.invoke_ = nullptr; o.destroy_ = nullptr; o.moveto_ = nullptr;
        }
        return *this;
    }

    EvtFn(const EvtFn&)            = delete;
    EvtFn& operator=(const EvtFn&) = delete;
    ~EvtFn() { reset(); }

    void operator()(const void* arg) const { if (invoke_ && ptr_) invoke_(ptr_, arg); }
    explicit operator bool() const { return invoke_ != nullptr; }

private:
    void reset() noexcept {
        if (ptr_) {
            if (destroy_) destroy_(ptr_);
            if (ptr_ != sbo_) free(ptr_);
        }
        ptr_ = nullptr; invoke_ = nullptr; destroy_ = nullptr; moveto_ = nullptr;
    }
};

#endif
