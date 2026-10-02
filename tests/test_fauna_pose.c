#include "ecology/fauna_pose.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void TestFootContact(void)
{
    FaunaPoseState s = { .gaitPhase = 0.1f, .movement = 1 };
    float travelled = 0.025f;
    FaunaFootPose a = FaunaPoseFoot(&s, 1, 0, 0, 2);
    s.gaitPhase += travelled / FaunaPoseStride(1);
    FaunaFootPose b = FaunaPoseFoot(&s, 1, 0, 0, 2);
    assert(a.planted && b.planted);
    /* A planted foot stays in world space as the body moves forward. */
    assert(fabsf(travelled + b.along - a.along) < 0.00001f);
    s.gaitPhase = 0.82f;
    FaunaFootPose swing = FaunaPoseFoot(&s, 1, 0, 0, 2);
    assert(!swing.planted && swing.lift > 0.10f);
    s.gaitPhase = 0.1f;
    FaunaPoseState still = FaunaPoseAdvance(s, (FaunaPoseInput){
        .action = FAUNA_ACTION_WANDER, .scale = 1
    }, 0.25f);
    assert(still.gaitPhase == s.gaitPhase);
    /* Opposite bipeds alternate; a six-legged body retains a tripod. */
    s.gaitPhase = 0.32f;
    int contacts = 0;
    for (unsigned row = 0; row < 3; row++) {
        for (unsigned side = 0; side < 2; side++) {
            contacts += FaunaPoseFoot(&s, 1, row, side, 3).planted;
        }
    }
    assert(contacts >= 3);
}

static void TestBehaviorBlends(void)
{
    FaunaPoseState s = {0};
    FaunaPoseInput input = {.action = FAUNA_ACTION_REST, .scale = 1, .speed = 1};
    s = FaunaPoseAdvance(s, input, 0.1f);
    assert(s.weights[FAUNA_ACTION_REST] == 0);
    input.speed = 0;
    for (int i = 0; i < 120; i++) s = FaunaPoseAdvance(s, input, 1.0f / 60);
    FaunaBodyPose resting = FaunaPoseEvaluate(&s, 0, 0, 0, false);
    assert(resting.bodyHeight < -0.3f);
    input.action = FAUNA_ACTION_FLEE;
    FaunaPoseState interrupted = FaunaPoseAdvance(s, input, 1.0f / 60);
    assert(interrupted.weights[FAUNA_ACTION_REST] > 0);
    assert(interrupted.weights[FAUNA_ACTION_REST] < s.weights[FAUNA_ACTION_REST]);
    for (int i = 0; i < 30; i++) interrupted = FaunaPoseAdvance(interrupted, input, 1.0f / 60);
    assert(interrupted.weights[FAUNA_ACTION_REST] < 0.01f);
    input.action = FAUNA_ACTION_DRINK;
    for (int i = 0; i < 120; i++) s = FaunaPoseAdvance(s, input, 1.0f / 60);
    FaunaBodyPose drinking = FaunaPoseEvaluate(&s, 0, 0, 0, false);
    assert(drinking.headHeight < -0.5f && drinking.headPitch > 0.7f);
    input.corpse = true;
    FaunaPoseState dead = FaunaPoseAdvance(s, input, 1);
    assert(memcmp(&s, &dead, sizeof(s)) == 0);
}

static void TestContinuityAndContacts(void)
{
    FaunaPoseState a = {.gaitPhase = 0.98f}, b = {.gaitPhase = 0.02f};
    FaunaPoseState mid = FaunaPoseInterpolate(a, b, 0.5f);
    assert(mid.gaitPhase < 0.001f || mid.gaitPhase > 0.999f);
    assert(FaunaPoseContactSeconds(0, 0.2f) == 0);
    assert(FaunaPoseContactSeconds(0.42f, 0.04f) > 0.039f);
    float single = FaunaPoseContactSeconds(0.95f, 0.8f), split = 0;
    for (int i = 0; i < 80; i++) {
        split += FaunaPoseContactSeconds(0.95f + i * 0.0125f, 0.01f);
    }
    assert(fabsf(single - split) < 0.00001f);
    assert(fabsf(single - 0.128f) < 0.00001f);
    a.weights[FAUNA_ACTION_REST] = NAN;
    assert(!FaunaPoseStateValid(&a));
    a = FaunaPoseAdvance(a, (FaunaPoseInput){.scale = 1}, 0.1f);
    assert(FaunaPoseStateValid(&a));
}

int main(void)
{
    TestFootContact();
    TestBehaviorBlends();
    TestContinuityAndContacts();
    puts("fauna pose tests passed");
    return 0;
}
