#include "presentation/tornado_renderer.h"

#include <math.h>

static float TornadoRenderUnit(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    return value >= 1.0f ? 1.0f : value;
}

static Vector3 TornadoPathPoint(const TornadoState *tornado, float height)
{
    float sway = sinf(tornado->rotation * 0.21f + height * 8.4f) *
                 tornado->radius * (0.06f + height * 0.12f);
    float velocityLength = hypotf(tornado->velocity.x, tornado->velocity.z);
    float sideX = velocityLength > 0.001f ?
        -tornado->velocity.z / velocityLength : 0.0f;
    float sideZ = velocityLength > 0.001f ?
        tornado->velocity.x / velocityLength : 1.0f;
    return (Vector3){
        tornado->center.x + tornado->velocity.x * height * 1.15f +
            sideX * sway,
        tornado->center.y + 0.15f + tornado->funnelHeight * height,
        tornado->center.z + tornado->velocity.z * height * 1.15f +
            sideZ * sway
    };
}

static Vector3 TornadoShellPoint(Vector3 center, float radius, float angle,
                                 float phase)
{
    float irregularity = 1.0f + 0.075f * sinf(angle * 3.0f + phase) +
                         0.035f * sinf(angle * 7.0f - phase * 0.63f);
    float localRadius = radius * irregularity;
    return (Vector3){
        center.x + cosf(angle) * localRadius,
        center.y,
        center.z + sinf(angle) * localRadius
    };
}

static void DrawTornadoOpenBand(Vector3 start, Vector3 end,
                                float startRadius, float endRadius,
                                int sides, float startPhase, float endPhase,
                                Color color)
{
    for (int side = 0; side < sides; side++) {
        float angle0 = 2.0f * PI * (float)side / (float)sides;
        float angle1 = 2.0f * PI * (float)(side + 1) / (float)sides;
        float angleMid = (angle0 + angle1) * 0.5f;
        float phaseMid = (startPhase + endPhase) * 0.5f;
        float wisp = 0.64f + 0.36f *
            (0.5f + 0.5f * sinf(angleMid * 4.0f + phaseMid * 1.7f));
        Color sideColor = color;
        sideColor.a = (unsigned char)((float)color.a * wisp);
        Vector3 lower0 = TornadoShellPoint(
            start, startRadius, angle0, startPhase);
        Vector3 lower1 = TornadoShellPoint(
            start, startRadius, angle1, startPhase);
        Vector3 upper0 = TornadoShellPoint(
            end, endRadius, angle0, endPhase);
        Vector3 upper1 = TornadoShellPoint(
            end, endRadius, angle1, endPhase);
        DrawTriangle3D(lower0, upper0, upper1, sideColor);
        DrawTriangle3D(lower0, upper1, lower1, sideColor);
    }
}

void DrawTornadoFunnel(const Camera3D *camera, const TornadoState *tornado,
                       GraphicsQuality quality, float daylight)
{
    if (!camera || !tornado || !tornado->active ||
        tornado->intensity <= 0.005f) {
        return;
    }
    float dx = camera->position.x - tornado->center.x;
    float dz = camera->position.z - tornado->center.z;
    if (dx * dx + dz * dz > 420.0f * 420.0f) return;

    int levels = quality == GRAPHICS_QUALITY_LOW ? 12 :
                 (quality == GRAPHICS_QUALITY_HIGH ? 28 : 20);
    int sides = quality == GRAPHICS_QUALITY_LOW ? 10 :
                (quality == GRAPHICS_QUALITY_HIGH ? 24 : 18);
    float condensation = TornadoRenderUnit(tornado->condensation);
    float light = TornadoRenderUnit(daylight);
    Color cloudColor = {
        (unsigned char)(62.0f + light * 58.0f),
        (unsigned char)(68.0f + light * 62.0f),
        (unsigned char)(74.0f + light * 66.0f),
        255
    };
    float visible = TornadoRenderUnit(
        tornado->intensity * (0.54f + condensation * 0.68f));

    for (int level = 0; level < levels; level++) {
        float lower = (float)level / (float)levels;
        float upper = (float)(level + 1) / (float)levels;
        Vector3 start = TornadoPathPoint(tornado, lower);
        Vector3 end = TornadoPathPoint(tornado, upper);
        float startRadius = tornado->radius *
            (0.18f + 0.88f * powf(lower, 0.72f));
        float endRadius = tornado->radius *
            (0.18f + 0.88f * powf(upper, 0.72f));
        unsigned char alpha = (unsigned char)(
            26.0f + visible * 68.0f);
        Color shell = cloudColor;
        shell.a = alpha;
        float startPhase = tornado->rotation * 0.31f + lower * 9.0f;
        float endPhase = tornado->rotation * 0.31f + upper * 9.0f;
        DrawTornadoOpenBand(start, end, startRadius, endRadius, sides,
                            startPhase, endPhase, shell);

        Color core = {
            (unsigned char)(cloudColor.r * 0.66f),
            (unsigned char)(cloudColor.g * 0.68f),
            (unsigned char)(cloudColor.b * 0.72f),
            (unsigned char)(alpha * 0.40f)
        };
        DrawTornadoOpenBand(start, end, startRadius * 0.58f,
                            endRadius * 0.58f, sides,
                            startPhase + 0.73f, endPhase + 0.73f, core);
    }

    float dust = TornadoRenderUnit(tornado->dustLoading);
    if (dust > 0.01f) {
        Vector3 base = tornado->center;
        base.y += 0.05f;
        Vector3 top = base;
        top.y += 2.6f + tornado->intensity * 2.8f;
        Color skirt = {
            112, 96, 73,
            (unsigned char)(26.0f + dust * 76.0f)
        };
        DrawTornadoOpenBand(base, top, tornado->radius * 1.52f,
                            tornado->radius * 0.56f, sides,
                            tornado->rotation * 0.22f,
                            tornado->rotation * 0.22f + 1.8f, skirt);
    }
}

void DrawTornadoOverlay(const Camera3D *camera, const TornadoState *tornado)
{
    if (!camera || !tornado || !tornado->active ||
        tornado->influenceRadius <= 0.0f) {
        return;
    }
    float dx = camera->position.x - tornado->center.x;
    float dz = camera->position.z - tornado->center.z;
    float distance = sqrtf(dx * dx + dz * dz);
    float circulation = TornadoRenderUnit(
        1.0f - distance / (tornado->influenceRadius * 1.15f));
    if (circulation <= 0.0f) return;
    float exposure = circulation * TornadoRenderUnit(tornado->intensity);
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    if (width <= 0 || height <= 0) return;
    DrawRectangleGradientV(
        0, (int)((float)height * 0.30f), width,
        (int)((float)height * 0.70f),
        Fade((Color){ 82, 88, 94, 255 }, exposure * 0.018f),
        Fade((Color){ 116, 94, 67, 255 },
             exposure * (0.045f + tornado->dustLoading * 0.10f)));
    DrawRectangle(0, 0, width, height,
                  Fade((Color){ 76, 82, 88, 255 }, exposure * 0.025f));
}
