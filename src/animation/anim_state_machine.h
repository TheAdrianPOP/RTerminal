#pragma once
#ifndef ANIM_STATE_MACHINE_H
#define ANIM_STATE_MACHINE_H

// Animation State Machine — drives an Animator through named states.
// Transitions can be conditioned on boolean or float parameters.
//
// Usage:
//   AnimStateMachine sm;
//   sm.addState(SID("idle"),  &idleClip);
//   sm.addState(SID("walk"),  &walkClip);
//   sm.addTransition(SID("idle"), SID("walk"), SID("isWalking"));
//   sm.setBool(SID("isWalking"), true);
//   sm.setEntry(SID("idle"));
//   sm.update(dt);  // auto-transitions, then updates animator

#include "anim_clip.h"
#include <cstring>

static constexpr int kMaxStates      = 32;
static constexpr int kMaxTransitions = 64;
static constexpr int kMaxParams      = 16;

enum class ConditionType { BoolTrue, BoolFalse, FloatGT, FloatLT, Always };

struct AnimCondition {
    StringID      param  = 0;
    ConditionType type   = ConditionType::Always;
    float         thresh = 0.f;
};

struct AnimTransition {
    StringID      from, to;
    AnimCondition condition;
    float         exitNorm = 1.f;   // normalised time to allow transition (0=any, 1=end)
    bool          hasExit  = false; // require clip to reach exitNorm before transitioning
};

struct AnimState {
    StringID       id = 0;
    const AnimClip* clip = nullptr;
    float          speed = 1.f;
};

struct AnimParam {
    StringID id = 0;
    enum { Bool, Float } type = Bool;
    union { bool bval; float fval; };
};

class AnimStateMachine {
public:
    AnimStateMachine() : stateCount_(0), transCount_(0), paramCount_(0), current_(-1) {}

    void addState(StringID id, const AnimClip* clip, float speed=1.f) {
        if (stateCount_>=kMaxStates) return;
        states_[stateCount_++] = {id, clip, speed};
    }
    void setEntry(StringID id) {
        int i = findState(id); if (i<0) return;
        current_ = i; animator_.play(states_[i].clip);
    }

    void addTransition(StringID from, StringID to, StringID param,
                       ConditionType cond=ConditionType::BoolTrue, float thresh=0.f,
                       float exitNorm=0.f) {
        if (transCount_>=kMaxTransitions) return;
        trans_[transCount_++] = {from, to, {param,cond,thresh}, exitNorm, exitNorm>0.f};
    }
    void addAlwaysTransition(StringID from, StringID to, float exitNorm=1.f) {
        if (transCount_>=kMaxTransitions) return;
        AnimTransition t; t.from=from; t.to=to; t.condition={0,ConditionType::Always,0};
        t.exitNorm=exitNorm; t.hasExit=(exitNorm>0.f);
        trans_[transCount_++]=t;
    }

    void setBool (StringID id, bool  v) { getOrCreateParam(id, AnimParam::Bool).bval=v; }
    void setFloat(StringID id, float v) { getOrCreateParam(id, AnimParam::Float).fval=v; }
    bool getBool (StringID id) const { const AnimParam* p=findParam(id); return p&&p->bval; }
    float getFloat(StringID id) const{ const AnimParam* p=findParam(id); return p?p->fval:0.f; }

    void update(float dt) {
        if (current_<0) return;
        float speed = states_[current_].speed;
        animator_.update(dt * speed);
        tryTransition();
    }

    Animator&       animator()       { return animator_; }
    const Animator& animator() const { return animator_; }
    StringID currentState() const { return current_>=0 ? states_[current_].id : 0; }

private:
    AnimState      states_[kMaxStates];
    AnimTransition trans_ [kMaxTransitions];
    AnimParam      params_[kMaxParams];
    int stateCount_, transCount_, paramCount_, current_;
    Animator       animator_;

    int findState(StringID id) const {
        for (int i=0;i<stateCount_;++i) if (states_[i].id==id) return i;
        return -1;
    }
    const AnimParam* findParam(StringID id) const {
        for (int i=0;i<paramCount_;++i) if (params_[i].id==id) return &params_[i];
        return nullptr;
    }
    AnimParam& getOrCreateParam(StringID id, int type) {
        for (int i=0;i<paramCount_;++i) if (params_[i].id==id) return params_[i];
        params_[paramCount_]={id,(AnimParam::Bool==type?AnimParam::Bool:AnimParam::Float),{}};
        return params_[paramCount_<kMaxParams?paramCount_++:paramCount_-1];
    }

    bool evalCondition(const AnimCondition& c) const {
        if (c.type==ConditionType::Always) return true;
        const AnimParam* p = findParam(c.param); if (!p) return false;
        switch (c.type) {
            case ConditionType::BoolTrue:  return p->bval;
            case ConditionType::BoolFalse: return !p->bval;
            case ConditionType::FloatGT:   return p->fval > c.thresh;
            case ConditionType::FloatLT:   return p->fval < c.thresh;
            default: return false;
        }
    }

    void tryTransition() {
        if (current_<0) return;
        StringID cur = states_[current_].id;
        for (int i=0;i<transCount_;++i) {
            auto& t = trans_[i]; if (t.from!=cur) continue;
            if (t.hasExit && animator_.normalizedTime() < t.exitNorm) continue;
            if (!evalCondition(t.condition)) continue;
            int next = findState(t.to); if (next<0) continue;
            current_ = next; animator_.play(states_[next].clip); return;
        }
    }
};

#endif
