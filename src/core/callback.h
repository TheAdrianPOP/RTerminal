#pragma once
#ifndef CALLBACK_H
#define CALLBACK_H

// VoidFn & EvtFn — minimal std::function replacement without <functional>.
// Move-only. Small-buffer optimised for closures <= 24 bytes.
// Compatible with non-capturing and capturing lambdas.

#include "move.h"
#include "internal.h"

// Minimal type traits for constructor disambiguation without <type_traits>
template<typename T, typename U> struct CbIsSame       { static constexpr bool value = false; };
template<typename T>             struct CbIsSame<T, T> { static constexpr bool value = true; };
template<bool B, typename T = void> struct CbEnableIf {};
template<typename T> struct CbEnableIf<true, T> { using type = T; };

// ── VoidFn ────────────────────────────────────────────────────────────────────
struct VoidFn {
    static constexpr size_t kSboSize = 24;

    void (*invoke_) (void*)        = nullptr;
    void (*destroy_)(void*)        = nullptr;
    void (*moveto_) (void*, void*) = nullptr;
    alignas(max_align_t) char sbo_[kSboSize] = {};
    void* ptr_                     = nullptr; // == sbo_ when SBO, else heap

    VoidFn() = default;

    template<typename F, typename = typename CbEnableIf<!CbIsSame<typename RmRef<F>::type, VoidFn>::value>::type>
    VoidFn(F&& f) {
        using Fn = typename RmRef<F>::type;
        invoke_  = [](void* p) { (*static_cast<Fn*>(p))(); };
        if constexpr (!__is_trivially_destructible(Fn)) {
            destroy_ = [](void* p) { static_cast<Fn*>(p)->~Fn(); };
        }
        moveto_  = [](void* dst, void* src) {
            new (dst) Fn(rt_move(*static_cast<Fn*>(src)));
        };

        if constexpr (sizeof(Fn) <= kSboSize) {
            new (sbo_) Fn(rt_forward<F>(f));
            ptr_ = sbo_;
        } else {
            ptr_ = malloc(sizeof(Fn));
            if (ptr_) {
                new (ptr_) Fn(rt_forward<F>(f));
            }
        }
    }

    VoidFn(VoidFn&& o) noexcept
        : invoke_(o.invoke_), destroy_(o.destroy_), moveto_(o.moveto_) {
        if (o.ptr_ == o.sbo_) {
            if (o.moveto_) o.moveto_(sbo_, o.sbo_);
            ptr_ = sbo_;
            if (o.destroy_) o.destroy_(o.sbo_); // Properly destruct the moved-from SBO closure
        } else {
            ptr_ = o.ptr_;
        }
        o.ptr_     = nullptr;
        o.invoke_  = nullptr;
        o.destroy_ = nullptr;
        o.moveto_  = nullptr;
    }

    VoidFn& operator=(VoidFn&& o) noexcept {
        if (this != &o) {
            reset();
            invoke_  = o.invoke_;
            destroy_ = o.destroy_;
            moveto_  = o.moveto_;

            if (o.ptr_ == o.sbo_) {
                if (o.moveto_) o.moveto_(sbo_, o.sbo_);
                ptr_ = sbo_;
                if (o.destroy_) o.destroy_(o.sbo_); // Properly destruct the moved-from SBO closure
            } else {
                ptr_ = o.ptr_;
            }
            o.ptr_     = nullptr;
            o.invoke_  = nullptr;
            o.destroy_ = nullptr;
            o.moveto_  = nullptr;
        }
        return *this;
    }

    VoidFn(const VoidFn&)            = delete;
    VoidFn& operator=(const VoidFn&) = delete;

    ~VoidFn() { reset(); }

    void operator()() const {
        if (invoke_ && ptr_) invoke_(const_cast<void*>(ptr_));
    }

    explicit operator bool() const { return invoke_ != nullptr && ptr_ != nullptr; }

    void reset() noexcept {
        if (ptr_) {
            if (destroy_) destroy_(ptr_);
            if (ptr_ != sbo_) free(ptr_);
        }
        ptr_     = nullptr;
        invoke_  = nullptr;
        destroy_ = nullptr;
        moveto_  = nullptr;
    }
};

// ── EvtFn ─────────────────────────────────────────────────────────────────────
struct EvtFn {
    static constexpr size_t kSboSize = 24;

    void (*invoke_) (void*, const void*) = nullptr;
    void (*destroy_)(void*)              = nullptr;
    void (*moveto_) (void*, void*)       = nullptr;
    alignas(max_align_t) char sbo_[kSboSize] = {};
    void* ptr_                           = nullptr;

    EvtFn() = default;

    template<typename F, typename = typename CbEnableIf<!CbIsSame<typename RmRef<F>::type, EvtFn>::value>::type>
    EvtFn(F&& f) {
        using Fn = typename RmRef<F>::type;
        invoke_  = [](void* p, const void* arg) { (*static_cast<Fn*>(p))(arg); };
        if constexpr (!__is_trivially_destructible(Fn)) {
            destroy_ = [](void* p) { static_cast<Fn*>(p)->~Fn(); };
        }
        moveto_  = [](void* dst, void* src) {
            new (dst) Fn(rt_move(*static_cast<Fn*>(src)));
        };

        if constexpr (sizeof(Fn) <= kSboSize) {
            new (sbo_) Fn(rt_forward<F>(f));
            ptr_ = sbo_;
        } else {
            ptr_ = malloc(sizeof(Fn));
            if (ptr_) {
                new (ptr_) Fn(rt_forward<F>(f));
            }
        }
    }

    EvtFn(EvtFn&& o) noexcept
        : invoke_(o.invoke_), destroy_(o.destroy_), moveto_(o.moveto_) {
        if (o.ptr_ == o.sbo_) {
            if (o.moveto_) o.moveto_(sbo_, o.sbo_);
            ptr_ = sbo_;
            if (o.destroy_) o.destroy_(o.sbo_); // Properly destruct the moved-from SBO closure
        } else {
            ptr_ = o.ptr_;
        }
        o.ptr_     = nullptr;
        o.invoke_  = nullptr;
        o.destroy_ = nullptr;
        o.moveto_  = nullptr;
    }

    EvtFn& operator=(EvtFn&& o) noexcept {
        if (this != &o) {
            reset();
            invoke_  = o.invoke_;
            destroy_ = o.destroy_;
            moveto_  = o.moveto_;

            if (o.ptr_ == o.sbo_) {
                if (o.moveto_) o.moveto_(sbo_, o.sbo_);
                ptr_ = sbo_;
                if (o.destroy_) o.destroy_(o.sbo_); // Properly destruct the moved-from SBO closure
            } else {
                ptr_ = o.ptr_;
            }
            o.ptr_     = nullptr;
            o.invoke_  = nullptr;
            o.destroy_ = nullptr;
            o.moveto_  = nullptr;
        }
        return *this;
    }

    EvtFn(const EvtFn&)            = delete;
    EvtFn& operator=(const EvtFn&) = delete;

    ~EvtFn() { reset(); }

    void operator()(const void* arg) const {
        if (invoke_ && ptr_) invoke_(const_cast<void*>(ptr_), arg);
    }

    explicit operator bool() const { return invoke_ != nullptr && ptr_ != nullptr; }

    void reset() noexcept {
        if (ptr_) {
            if (destroy_) destroy_(ptr_);
            if (ptr_ != sbo_) free(ptr_);
        }
        ptr_     = nullptr;
        invoke_  = nullptr;
        destroy_ = nullptr;
        moveto_  = nullptr;
    }
};

#endif // CALLBACK_H