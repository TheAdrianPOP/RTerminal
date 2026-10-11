#pragma once
#ifndef DSP_H
#define DSP_H

#include "math_utils.h"

// ── Base effect ───────────────────────────────────────────────────────────────

struct DspEffect {
    bool enabled = true;
    float wet    = 1.f;  // mix: 0=dry, 1=fully wet
    virtual ~DspEffect() = default;
    // Process 'frames' stereo samples. L and R are separate float arrays.
    virtual void process(float* L, float* R, int frames) = 0;
};

// ── Low-pass filter (one-pole IIR) ────────────────────────────────────────────

struct DspLowPass : DspEffect {
    float alpha = 0.5f;  // [0,1] — 0=no filter, 1=silence
    float prevL = 0.f, prevR = 0.f;

    DspLowPass(float cutoffHz, float sampleRate) {
        float w = 2.f * 3.14159f * cutoffHz / sampleRate;
        float c = 2.f - cosf(w);
        alpha   = c - sqrtf(c*c - 1.f);
        alpha   = fast_clamp(alpha, 0.f, 1.f);
    }
    void process(float* L, float* R, int n) override {
        for (int i=0; i<n; ++i) {
            prevL = prevL + (1-alpha)*(L[i]-prevL); L[i]=prevL;
            prevR = prevR + (1-alpha)*(R[i]-prevR); R[i]=prevR;
        }
    }
};

// ── High-pass filter ──────────────────────────────────────────────────────────

struct DspHighPass : DspEffect {
    float alpha = 0.99f;
    float prevInL=0,prevInR=0,prevOutL=0,prevOutR=0;

    DspHighPass(float cutoffHz, float sampleRate) {
        float dt = 1.f/sampleRate, rc = 1.f/(2*3.14159f*cutoffHz);
        alpha = rc/(rc+dt);
    }
    void process(float* L, float* R, int n) override {
        for (int i=0;i<n;++i) {
            float ol = alpha*(prevOutL + L[i]-prevInL);
            float or_ = alpha*(prevOutR + R[i]-prevInR);
            prevInL=L[i]; prevInR=R[i]; prevOutL=ol; prevOutR=or_;
            L[i]=ol; R[i]=or_;
        }
    }
};

// ── Gain / volume ─────────────────────────────────────────────────────────────

struct DspGain : DspEffect {
    float gain = 1.f;
    explicit DspGain(float g) : gain(g) {}
    void process(float* L, float* R, int n) override {
        for (int i=0;i<n;++i){ L[i]*=gain; R[i]*=gain; }
    }
};

// ── Reverb (simple Schroeder comb+allpass) ────────────────────────────────────

struct DspReverb : DspEffect {
    static constexpr int kComb   = 4;
    static constexpr int kAllPass = 2;
    static constexpr int kBufMax = 4096;

    struct DelayLine { float buf[kBufMax]={}; int len,pos=0;
        void set(int l){ len=fast_clamp(l,1,kBufMax); }
        float tick(float in, float fb){ float o=buf[pos]; buf[pos]=(float)fmod(buf[pos]*fb+in,1e6f); pos=(pos+1)%len; return o; }
    };

    float decay    = 0.5f;
    float mix      = 0.25f;
    DelayLine combs[kComb];
    DelayLine allpasses[kAllPass];

    DspReverb(float sampleRate=44100.f) {
        int c[]={1557,1617,1491,1422}; for(int i=0;i<kComb;++i)combs[i].set((int)(c[i]*sampleRate/44100));
        int a[]={225,556}; for(int i=0;i<kAllPass;++i)allpasses[i].set((int)(a[i]*sampleRate/44100));
    }
    void process(float* L, float* R, int n) override {
        for (int i=0;i<n;++i) {
            float in=(L[i]+R[i])*0.5f, out=0;
            for (int c=0;c<kComb;++c) out+=combs[c].tick(in,decay);
            out*=0.25f;
            for (int a=0;a<kAllPass;++a) { float d=allpasses[a].buf[allpasses[a].pos]; out=d+out*(1-0.7f); allpasses[a].buf[allpasses[a].pos]=in+out*0.7f; allpasses[a].pos=(allpasses[a].pos+1)%allpasses[a].len; }
            L[i]=fast_lerp(L[i],out,mix); R[i]=fast_lerp(R[i],out,mix);
        }
    }
};

// ── Distortion / clipping ─────────────────────────────────────────────────────

struct DspDistortion : DspEffect {
    float drive = 2.f;  // >1 = harder clip
    void process(float* L, float* R, int n) override {
        for (int i=0;i<n;++i) {
            L[i]=fast_clamp(L[i]*drive,-1.f,1.f);
            R[i]=fast_clamp(R[i]*drive,-1.f,1.f);
        }
    }
};

// ── Effect chain ──────────────────────────────────────────────────────────────

struct DspChain {
    PodVector<DspEffect*> effects;

    ~DspChain() { clear(); }

    template<typename E, typename... Args>
    E* add(Args&&... args) {
        E* e = new E(rt_forward<Args>(args)...);
        effects.push_back(e);
        return e;
    }

    void process(float* L, float* R, int frames) {
        for (DspEffect* e : effects) if (e && e->enabled) e->process(L, R, frames);
    }
    void clear() {
        for (DspEffect* e : effects) delete e;
        effects.clear();
    }
};

#endif
