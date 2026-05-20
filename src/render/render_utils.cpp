#include "render_utils.h"

RampCache* tl_ramp_slots = nullptr;

void initRampCache() {
    if (tl_ramp_slots) return;
    tl_ramp_slots = new RampCache[RT_RAMP_SLOTS];
}