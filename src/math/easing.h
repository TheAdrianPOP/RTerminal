#pragma once
#ifndef EASING_H
#define EASING_H

#include "math_utils.h"

namespace Ease {

FORCE_INLINE float linear    (float t) { return t; }

FORCE_INLINE float inQuad    (float t) { return t*t; }
FORCE_INLINE float outQuad   (float t) { return t*(2.f-t); }
FORCE_INLINE float inOutQuad (float t) { return t<0.5f?2*t*t:(-2*t*t+4*t-1); }

FORCE_INLINE float inCubic   (float t) { return t*t*t; }
FORCE_INLINE float outCubic  (float t) { float s=t-1; return s*s*s+1; }
FORCE_INLINE float inOutCubic(float t) { return t<0.5f?4*t*t*t:(4*t*t*t-12*t*t+12*t-3); }

FORCE_INLINE float inQuart   (float t) { return t*t*t*t; }
FORCE_INLINE float outQuart  (float t) { float s=t-1; return 1-s*s*s*s; }
FORCE_INLINE float inOutQuart(float t) { float s=t-1; return t<0.5f?8*t*t*t*t:1-8*s*s*s*s; }

FORCE_INLINE float inSine    (float t) { return 1.f - fast_cos(t * HALF_PI); }
FORCE_INLINE float outSine   (float t) { return fast_sin(t * HALF_PI); }
FORCE_INLINE float inOutSine (float t) { return 0.5f * (1.f - fast_cos(t * PI)); }

FORCE_INLINE float inExpo    (float t) { return t <= 0.f ? 0.f : fast_pow(2.f, 10.f*t - 10.f); }
FORCE_INLINE float outExpo   (float t) { return t >= 1.f ? 1.f : 1.f - fast_pow(2.f, -10.f*t); }
FORCE_INLINE float inOutExpo (float t) {
    if (t <= 0.f) return 0.f; if (t >= 1.f) return 1.f;
    return t < 0.5f
        ? fast_pow(2.f, 20.f*t - 10.f) / 2.f
        : (2.f - fast_pow(2.f, -20.f*t + 10.f)) / 2.f;
}

FORCE_INLINE float inElastic  (float t) {
    if (t <= 0.f) return 0.f; if (t >= 1.f) return 1.f;
    return -fast_pow(2.f, 10.f*t-10.f) * fast_sin((t*10.f-10.75f)*(TWO_PI/3.f));
}
FORCE_INLINE float outElastic (float t) {
    if (t <= 0.f) return 0.f; if (t >= 1.f) return 1.f;
    return fast_pow(2.f,-10.f*t)*fast_sin((t*10.f-0.75f)*(TWO_PI/3.f))+1.f;
}

FORCE_INLINE float inBack    (float t) { const float c=1.70158f; return t*t*((c+1)*t-c); }
FORCE_INLINE float outBack   (float t) { const float c=1.70158f; float s=t-1; return s*s*((c+1)*s+c)+1; }

FORCE_INLINE float outBounce (float t) {
    const float d1=7.5625f, d2=2.75f;
    if      (t < 1.f/d2)    return d1*t*t;
    else if (t < 2.f/d2)  { t-=1.5f/d2;   return d1*t*t+0.75f; }
    else if (t < 2.5f/d2) { t-=2.25f/d2;  return d1*t*t+0.9375f; }
    else                   { t-=2.625f/d2; return d1*t*t+0.984375f; }
}
FORCE_INLINE float inBounce  (float t) { return 1.f - outBounce(1.f - t); }

FORCE_INLINE float smoothStep (float t) { t=fast_clamp(t,0.f,1.f); return t*t*(3-2*t); }
FORCE_INLINE float smootherStep(float t){ t=fast_clamp(t,0.f,1.f); return t*t*t*(t*(t*6-15)+10); }

FORCE_INLINE float mapRange(float t, float a, float b) { return a + t*(b-a); }

template<typename EaseFn>
FORCE_INLINE float ease(float t, EaseFn fn, float from=0.f, float to=1.f) {
    return from + fn(fast_clamp(t,0.f,1.f)) * (to - from);
}

} // namespace Ease

#endif // EASING_H