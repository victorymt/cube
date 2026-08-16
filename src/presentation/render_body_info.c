#include "presentation/render.h"

#include "gameplay/ship.h"
#include "space/space_remnant.h"
#include "space/space_units.h"

#include <math.h>

void DrawBodyInfoPanel(const SpaceBodyInfo *body)
{
    if (!body) return;

    const char *typeName = body->isStar ? SpectrumName(body->spectrum)
                                        : SolarStyleName(body->style);
    const char *line1;
    const char *line2 = NULL;
    const char *line3 = NULL;
    if (body->isStar) {
        double distanceAu = SpaceUnitsGameDistanceToKilometers(body->dist) /
                            SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
        line1 = TextFormat("%s - %s - %.3g AU", body->name, typeName,
                           distanceAu);
        line2 = TextFormat("M %.2f Msol  L %.2g Lsol  T %.0f K",
                           body->hostStar.massSolar,
                           body->hostStar.luminositySolar,
                           body->hostStar.temperatureK);
        if (body->remnant.active) {
            line3 = TextFormat(
                "SNR %.2g kyr  shock %.2g pc  hazard %.2f",
                body->remnant.ageYears / 1000.0,
                body->remnant.physicalShockRadiusKm / SPACE_REMNANT_PARSEC_KM,
                SpaceRemnantRadiationHazardAtDistance(&body->remnant,
                                                       body->dist));
        } else {
            line3 = TextFormat("Age %.2g Gyr  Luminous life %.2g Gyr",
                               body->hostStar.ageGyr,
                               body->hostStar.luminousLifetimeGyr);
        }
    } else {
        float surfaceGap = fabsf(body->dist - SolarBodyTerrainProxyRadius(
            body->spaceProxyRadius));
        line1 = TextFormat("%s - %s - %.0f K - %.2f g", body->name, typeName,
                           body->profile.equilibriumTempK,
                           body->profile.surfaceGravity);
        if (!body->profile.hasSolidSurface) {
            line2 = "Dense gas envelope - no solid surface";
        } else if (ShipIsDriving() && surfaceGap <= 20.0f) {
            line2 = TextFormat("%s - E land",
                               PlanetAtmosphereName(
                                   body->profile.atmosphereType));
        } else {
            double distanceKm = SpaceUnitsGameDistanceToKilometers(body->dist);
            line2 = TextFormat("%s - %.3g km",
                               PlanetAtmosphereName(
                                   body->profile.atmosphereType),
                               distanceKm);
        }
        if (body->remnantEnvironment.active) {
            line3 = TextFormat(
                "remnant hazard %.2f  ejecta %.2f  shell %.0f blocks",
                body->remnantEnvironment.radiationHazard,
                body->remnantEnvironment.ejectaDensity,
                body->remnantEnvironment.nearestShellDistanceGame);
        }
    }

    int sw = GetScreenWidth();
    int maxWidth = (int)fmaxf((float)sw - 64.0f, 120.0f);
    int fs = 18;
    int detailFs = 16;
    while (fs > 13 && UiMeasureText(line1, fs) > maxWidth) fs--;
    while (detailFs > 12 &&
           ((line2 && UiMeasureText(line2, detailFs) > maxWidth) ||
            (line3 && UiMeasureText(line3, detailFs) > maxWidth))) {
        detailFs--;
    }
    int width = UiMeasureText(line1, fs);
    if (line2) width = fmaxf((float)width,
                             (float)UiMeasureText(line2, detailFs));
    if (line3) width = fmaxf((float)width,
                             (float)UiMeasureText(line3, detailFs));
    int x = sw / 2 - width / 2;
    int y = 64;
    float height = line3 ? 84.0f : (line2 ? 62.0f : 40.0f);
    Rectangle panel = {
        (float)x - 16.0f, (float)y - 8.0f, (float)width + 32.0f, height
    };
    DrawRectangleRounded(panel, 0.10f, 6, Fade(BLACK, 0.55f));
    DrawRectangleRoundedLinesEx(panel, 0.10f, 6, 1.5f,
                                Fade(WHITE, 0.30f));
    UiDrawText(line1, x, y, fs, WHITE);
    if (line2) UiDrawText(line2, x, y + 24, detailFs, Fade(WHITE, 0.82f));
    if (line3) UiDrawText(line3, x, y + 46, detailFs, Fade(WHITE, 0.72f));
}
