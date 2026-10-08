#include "world/volcano_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool loaded = true;
static bool Ready(const VolcanoDefinition *d, void *userData)
{
    (void)d; (void)userData;
    return loaded;
}
static VolcanoContext Context(void)
{
    return (VolcanoContext){ .valid = true, .bodyId = 7u, .seed = 123u,
        .observerMap = { 0, 20, 0 }, .ready = Ready };
}
static VolcanoDefinition Definition(int x)
{
    return VolcanoDefine(7u, 123u, x, 20, 0, 0.8f, true);
}
static VolcanoRecord First(void)
{
    VolcanoRecord result;
    assert(VolcanoRuntimeAt(0u, &result));
    return result;
}
static void Step(unsigned count, VolcanoContext context)
{
    for (unsigned i = 0u; i < count; i++) VolcanoRuntimeUpdate(0.5, &context);
}
static void AssertSame(VolcanoRecord a, VolcanoRecord b)
{
    assert(a.definition.id == b.definition.id && a.state.phase == b.state.phase);
    assert(a.state.activeTicks == b.state.activeTicks && a.state.event == b.state.event);
    assert(a.state.phaseTicks == b.state.phaseTicks && a.state.eruptionTicks == b.state.eruptionTicks);
    assert(a.state.pressure == b.state.pressure && a.state.reservoir == b.state.reservoir);
    assert(a.active == b.active && fabs(a.accumulator - b.accumulator) < 1e-8);
}

static void TestIdentityAndLifecycle(void)
{
    VolcanoDefinition d = Definition(24);
    VolcanoDefinition alias = VolcanoDefine(7u, 123u,
        24 + SURFACE_EQUATOR_BLOCKS, 20, 0, 0.8f, true);
    assert(d.id == alias.id && d.mapX == alias.mapX && d.mapZ == alias.mapZ);
    VolcanoDefinition pole = VolcanoDefine(7u, 123u,
        24 + SURFACE_EQUATOR_BLOCKS / 2, 20, SURFACE_POLE_TO_POLE_BLOCKS, 0.8f, true);
    assert(d.id == pole.id);
    VolcanoRuntimeReset();
    assert(VolcanoRuntimeForce(d, 1.0f, 4u));
    assert(!VolcanoRuntimeForce(alias, 1.0f, 4u));
    assert(VolcanoRuntimeStats().count == 1u);
    Step(59u, Context());
    assert(First().state.phase == VOLCANO_UNREST);
    Step(1u, Context());
    assert(First().state.phase == VOLCANO_ERUPTING);
    Step(4u, Context());
    assert(First().state.phase == VOLCANO_WANING);
    Step(60u, Context());
    assert(First().state.phase == VOLCANO_COOLING);
    Step(120u, Context());
    assert(First().state.phase == VOLCANO_RECOVERY);
    assert(First().state.reservoir == 256.0f);
    assert(VolcanoRuntimeForce(d, 0.5f, 10u));
    assert(First().state.event == 2u);
    assert(VolcanoRuntimeStop(d.id));
    assert(First().state.phase == VOLCANO_COOLING);
}

static void TestParkingAndFramePartitions(void)
{
    VolcanoRuntimeReset();
    assert(VolcanoRuntimeForce(Definition(24), 1.0f, 300u));
    Step(80u, Context());
    VolcanoRecord baseline = First();
    VolcanoRuntimeReset();
    assert(VolcanoRuntimeForce(Definition(24), 1.0f, 300u));
    VolcanoContext context = Context();
    for (unsigned i = 0u; i < 2400u; i++) VolcanoRuntimeUpdate(1.0 / 60.0, &context);
    AssertSame(First(), baseline);
    uint64_t ticks = First().state.activeTicks;
    loaded = false;
    Step(100u, context);
    assert(!First().active && First().state.activeTicks == ticks);
    loaded = true;
    context.observerMap.x = 1000;
    Step(100u, context);
    assert(First().state.activeTicks == ticks);
    context = Context();
    context.bodyId = 8u;
    Step(100u, context);
    assert(First().state.activeTicks == ticks);
    context = Context();
    Step(1u, context);
    assert(First().state.activeTicks == ticks + 1u && First().state.event == 1u);
    VolcanoRecord before = First();
    VolcanoRecord query;
    for (unsigned i = 0u; i < 100u; i++) {
        assert(VolcanoRuntimeNearest(7u, context.observerMap, &query));
        (void)VolcanoRuntimeStats();
    }
    AssertSame(First(), before);
    VolcanoRuntimeUpdate(NAN, &context);
    VolcanoRuntimeUpdate(10000.0, &context);
    AssertSame(First(), before);
}

static bool PeakSample(SurfaceAddress address, float *potential, int *height, void *context)
{
    const SurfaceAddress *peak = context;
    *potential = address.face == peak->face && address.u == peak->u && address.v == peak->v ? 0.9f : 0.2f;
    *height = 20;
    return true;
}
static void TestNaturalCandidate(void)
{
    SurfaceAddress peak = { 7u, SURFACE_FACE_POS_Y, 64, 64, 0 };
    VolcanoDefinition definition;
    assert(VolcanoCandidateAt(peak, 123u, PeakSample, &peak, &definition));
    assert(definition.id == VolcanoId(peak));
    SurfaceAddress neighbor = SurfaceAddressOffset(peak, 128, 0, 0);
    assert(!VolcanoCandidateAt(neighbor, 123u, PeakSample, &peak, &definition));
    assert(VolcanoCandidateAt(peak, 123u, PeakSample, &peak, &definition));
    VolcanoState state = VolcanoModelCreate(&definition);
    assert(state.reservoir == 4096.0f);
    for (unsigned i = 0u; i < 8000u && state.phase == VOLCANO_DORMANT; i++) {
        VolcanoModelStep(&state, &definition);
    }
    assert(state.phase == VOLCANO_UNREST && state.event == 1u);
    VolcanoRuntimeReset();
    VolcanoContext context = Context();
    context.discover = true;
    context.sample = PeakSample;
    context.userData = &peak;
    context.observerMap = (Vector3){ definition.mapX, 20, definition.mapZ };
    VolcanoRecord queried;
    assert(!VolcanoRuntimeNearest(7u, context.observerMap, &queried));
    assert(VolcanoRuntimeStats().count == 0u);
    Step(4u, context);
    assert(VolcanoRuntimeNearest(7u, context.observerMap, &queried));
    assert(queried.definition.id == definition.id && queried.active);
    unsigned count = VolcanoRuntimeStats().count;
    VolcanoDefinition forced = VolcanoDefine(7u, 123u, definition.mapX,
        definition.address.radial, definition.mapZ, 1.0f, true);
    assert(VolcanoRuntimeForce(forced, 1.0f, 100u));
    assert(VolcanoRuntimeStats().count == count);
    assert(VolcanoRuntimeNearest(7u, context.observerMap, &queried));
    assert(queried.definition.id == definition.id && queried.state.event == 1u);
}

static void TestCapacity(void)
{
    VolcanoRuntimeReset();
    for (unsigned i = 0u; i < VOLCANO_MAX_RECORDS; i++) {
        assert(VolcanoRuntimeForce(Definition((int)i * 128), 1.0f, 4u));
    }
    assert(!VolcanoRuntimeForce(Definition(-1024), 1.0f, 4u));
    assert(VolcanoRuntimeStats().count == VOLCANO_MAX_RECORDS);
    assert(VolcanoRuntimeStats().rejectedAdmissions == 1u);
    Step(1u, Context());
    assert(VolcanoRuntimeStats().active == VOLCANO_MAX_ACTIVE);
    assert(First().state.activeTicks == 1u);
    VolcanoRecord last;
    assert(VolcanoRuntimeAt(VOLCANO_MAX_RECORDS - 1u, &last));
    assert(last.state.activeTicks == 0u);
}

static void TestPersistence(void)
{
    VolcanoRuntimeReset();
    VolcanoContext context = Context();
    assert(VolcanoRuntimeForce(Definition(24), 1.0f, 100u));
    Step(62u, context);
    VolcanoRuntimeSolidifiedSource(8u, 24, 20, 0);
    VolcanoRuntimeSolidifiedSource(7u, 25, 20, 0);
    assert(First().definition.address.radial == 20);
    VolcanoRuntimeSolidifiedSource(7u, 24, 20, 0);
    VolcanoRuntimeSolidifiedSource(7u, 24, 20, 0);
    assert(First().definition.address.radial == 21);
    VolcanoRuntimeUpdate(0.125, &context);
    VolcanoRecord before = First();
    FILE *file = tmpfile();
    assert(file && VolcanoRuntimeSave(file));
    long length = ftell(file);
    assert(length > 100 && length < 4096);
    unsigned char bytes[4096];
    rewind(file);
    assert(fread(bytes, 1u, (size_t)length, file) == (size_t)length);
    rewind(file);
    VolcanoRuntimeReset();
    assert(VolcanoRuntimeLoad(file));
    fclose(file);
    AssertSame(First(), before);
    assert(First().definition.address.radial == 21);
    Step(20u, context);
    VolcanoRecord expected = First();
    file = tmpfile();
    assert(file && fwrite(bytes, 1u, (size_t)length, file) == (size_t)length);
    rewind(file);
    assert(VolcanoRuntimeLoad(file));
    fclose(file);
    for (unsigned i = 0u; i < 80u; i++) VolcanoRuntimeUpdate(0.125, &context);
    AssertSame(First(), expected);
    for (unsigned cut = 0u; cut < (unsigned)length; cut++) {
        file = tmpfile();
        assert(file && fwrite(bytes, 1u, cut, file) == cut);
        rewind(file);
        assert(!VolcanoRuntimeLoad(file));
        fclose(file);
        AssertSame(First(), expected);
    }
    bytes[8] = 255u; /* oversized record count */
    file = tmpfile();
    assert(file && fwrite(bytes, 1u, (size_t)length, file) == (size_t)length);
    rewind(file);
    assert(!VolcanoRuntimeLoad(file));
    fclose(file);
    AssertSame(First(), expected);
}

int main(void)
{
    TestIdentityAndLifecycle();
    TestParkingAndFramePartitions();
    TestNaturalCandidate();
    TestCapacity();
    TestPersistence();
    puts("volcano model/runtime tests passed");
    return 0;
}
