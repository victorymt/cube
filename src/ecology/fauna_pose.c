#include "ecology/fauna_pose.h"

#include <math.h>

#define POSE_PI 3.14159265358979323846f
#define STRIKE_RATE 1.25f

static float Clamp(float x, float lo, float hi)
{
    return isfinite(x) ? fminf(fmaxf(x, lo), hi) : lo;
}

static float Cycle(float x)
{
    return x - floorf(x);
}

float FaunaPoseStride(float scale)
{
    return 0.85f * Clamp(scale, 0.2f, 3.0f);
}

bool FaunaPoseStateValid(const FaunaPoseState *state)
{
    if (!state || !isfinite(state->gaitPhase) || state->gaitPhase < 0 ||
        state->gaitPhase >= 1 || !isfinite(state->actionPhase) ||
        state->actionPhase < 0 || state->actionPhase >= 1 ||
        !isfinite(state->movement) || state->movement < 0 || state->movement > 1 ||
        !isfinite(state->turn) || fabsf(state->turn) > 1 ||
        !isfinite(state->engagement) || state->engagement < 0 ||
        state->engagement > 1) return false;
    for (int i = 0; i < FAUNA_ACTION_COUNT; i++) {
        if (!isfinite(state->weights[i]) || state->weights[i] < 0 ||
            state->weights[i] > 1) return false;
    }
    return true;
}

FaunaPoseState FaunaPoseAdvance(FaunaPoseState state,
                                FaunaPoseInput input, float dt)
{
    if (!FaunaPoseStateValid(&state)) state = (FaunaPoseState){0};
    dt = Clamp(dt, 0, 1);
    if (dt == 0 || input.corpse) return state;
    float blend = 1.0f - expf(-8.0f * dt);
    float speed = Clamp(input.speed, 0, 20);
    state.gaitPhase = Cycle(state.gaitPhase +
        Clamp(input.distance, 0, 20 * dt) / FaunaPoseStride(input.scale));
    state.movement += (Clamp(speed / 0.35f, 0, 1) - state.movement) * blend;
    state.turn += (Clamp(input.turnRate / 2.0f, -1, 1) - state.turn) * blend;
    state.engagement += ((input.engaged ? 1.0f : 0.0f) - state.engagement) * blend;
    state.actionPhase = input.engaged
        ? Cycle(state.actionPhase + dt * STRIKE_RATE) : 0.0f;
    for (int i = 0; i < FAUNA_ACTION_COUNT; i++) {
        float target = i == (int)input.action ? 1.0f : 0.0f;
        /* The body must brake before lowering its head or folding its legs. */
        if (i == FAUNA_ACTION_REST || i == FAUNA_ACTION_NEST ||
            i == FAUNA_ACTION_FORAGE || i == FAUNA_ACTION_DRINK) {
            target *= 1.0f - Clamp(speed / 0.30f, 0, 1);
        }
        float rate = input.action == FAUNA_ACTION_FLEE ? 14.0f : 4.5f;
        state.weights[i] += (target - state.weights[i]) * (1 - expf(-rate * dt));
    }
    return state;
}

static float LerpCycle(float a, float b, float t)
{
    float delta = b - a;
    if (delta < -0.5f) delta += 1;
    if (delta > 0.5f) delta -= 1;
    return Cycle(a + delta * t);
}

FaunaPoseState FaunaPoseInterpolate(FaunaPoseState from,
                                    FaunaPoseState to, float alpha)
{
    float t = Clamp(alpha, 0, 1);
    FaunaPoseState result = to;
    result.gaitPhase = LerpCycle(from.gaitPhase, to.gaitPhase, t);
    result.actionPhase = LerpCycle(from.actionPhase, to.actionPhase, t);
    result.movement = from.movement + (to.movement - from.movement) * t;
    result.turn = from.turn + (to.turn - from.turn) * t;
    result.engagement = from.engagement + (to.engagement - from.engagement) * t;
    for (int i = 0; i < FAUNA_ACTION_COUNT; i++) {
        result.weights[i] = from.weights[i] + (to.weights[i] - from.weights[i]) * t;
    }
    return result;
}

FaunaBodyPose FaunaPoseEvaluate(const FaunaPoseState *s, float phase,
                                float fatigue, float stress, bool airborne)
{
    FaunaBodyPose p = {0};
    if (!s) return p;
    const float *w = s->weights;
    float rest = w[FAUNA_ACTION_REST], nest = w[FAUNA_ACTION_NEST];
    float feed = w[FAUNA_ACTION_FORAGE] + w[FAUNA_ACTION_SCAVENGE] * s->engagement;
    float drink = w[FAUNA_ACTION_DRINK], alert = w[FAUNA_ACTION_ALERT];
    float run = w[FAUNA_ACTION_FLEE], hunt = w[FAUNA_ACTION_HUNT];
    float tired = Clamp(fatigue, 0, 1), tense = Clamp(stress, 0, 1);
    float chew = sinf(phase * 7.0f);
    float scan = sinf(phase * 0.73f) * sinf(phase * 0.31f);
    p.crouch = Clamp(rest * 0.85f + nest * 0.65f + hunt * 0.18f +
        w[FAUNA_ACTION_SEEK_SHELTER] * 0.16f, 0, 1);
    p.display = w[FAUNA_ACTION_MATE] * s->engagement;
    float attack = sinf(POSE_PI * Clamp((s->actionPhase - 0.22f) / 0.55f, 0, 1));
    p.strike = hunt * s->engagement * attack * attack;
    p.bodyHeight = -0.48f * p.crouch +
        sinf(phase * 1.8f) * 0.012f * (1 - tense * 0.5f) +
        sinf(s->gaitPhase * 4 * POSE_PI) * 0.035f * s->movement;
    p.bodyPitch = run * 0.12f + hunt * 0.10f + tired * 0.035f - p.display * 0.10f;
    p.bodyRoll = -s->turn * (airborne ? 0.38f : 0.055f);
    p.headHeight = alert * 0.25f - feed * (0.50f + chew * 0.025f) -
        drink * 0.62f - rest * 0.18f - tired * 0.06f + p.display * 0.12f;
    p.headPitch = feed * (0.65f + chew * 0.05f) + drink * 0.80f +
        rest * 0.22f - alert * 0.26f - p.display * 0.18f;
    p.headYaw = scan * 0.24f * (1 - feed) * (1 - drink) * (1 - hunt) *
        (1 - alert) * (1 - rest) + run * sinf(phase * 0.8f) * 0.30f;
    p.headForward = feed * 0.10f + drink * 0.20f + p.strike * 0.28f - rest * 0.12f;
    p.tailYaw = sinf(phase * (2.0f + run * 2.0f)) *
        (0.07f + p.display * 0.35f) - s->turn * 0.22f;
    if (airborne) { p.bodyHeight *= 0.15f; p.crouch *= 0.2f; }
    return p;
}

FaunaFootPose FaunaPoseFoot(const FaunaPoseState *s, float scale,
                            unsigned pair, unsigned side, unsigned pairs)
{
    FaunaFootPose foot = { .planted = true };
    if (!s) return foot;
    float run = s->weights[FAUNA_ACTION_FLEE];
    /* A walking quadruped uses a four-beat sequence, running uses diagonal
     * pairs. Hexapods alternate tripods; bipeds alternate left and right. */
    float pairOffset = pairs == 2 ? (0.25f + run * 0.25f) : 0.5f;
    float u = Cycle(s->gaitPhase + (float)side * 0.5f + (float)pair * pairOffset);
    float duty = 0.66f - run * 0.16f;
    float stride = FaunaPoseStride(scale);
    float reach = stride * duty;
    if (u < duty) {
        foot.along = reach * 0.5f - stride * u;
    } else {
        float t = (u - duty) / (1 - duty);
        /* Hermite swing meets the stance with matching horizontal velocity. */
        float smooth = t * t * (3 - 2 * t);
        float tangent = t * (1 - t) * (1 - 2 * t);
        foot.along = -reach * 0.5f + reach * smooth - stride * (1 - duty) * tangent;
        foot.lift = sinf(POSE_PI * t) * sinf(POSE_PI * t) *
            Clamp(scale, 0.2f, 3) * (0.16f + run * 0.09f);
        foot.planted = false;
    }
    foot.along *= s->movement;
    foot.lift *= s->movement;
    return foot;
}

float FaunaPoseContactSeconds(float previousPhase, float dt)
{
    if (!isfinite(previousPhase) || !isfinite(dt) || dt <= 0) return 0;
    float start = Cycle(previousPhase), end = start + fminf(dt, 1) * STRIKE_RATE;
    float overlap = 0;
    for (int cycle = 0; cycle <= 2; cycle++) {
        overlap += fmaxf(0, fminf(end, cycle + 0.56f) - fmaxf(start, cycle + 0.40f));
    }
    return overlap / STRIKE_RATE;
}
