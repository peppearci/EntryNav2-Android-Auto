#include "cdr_weather_hook.hpp"
#include "app_log.hpp"

#include <dlfcn.h>
#include <cstdio>
#include <cstring>

namespace evo {
namespace cdr_weather {

using ApplyTextFn = void (*)(void*);
using ApplyIconFn = void (*)(void*);
using ApplyGraphicFn = void (*)(void*);

static void* s_cdrApp = nullptr;
static char s_lastCity[48] = "";
static char s_lastTemp[16] = "";

void update(const weather::WeatherData& weather, const weather::DayForecast& today) {
    if (!weather.valid) return;

    // Only update if values changed
    if (strcmp(s_lastCity, weather.city) == 0 && strcmp(s_lastTemp, today.tempMax) == 0) {
        return;
    }

    strncpy(s_lastCity, weather.city, sizeof(s_lastCity) - 1);
    strncpy(s_lastTemp, today.tempMax, sizeof(s_lastTemp) - 1);

    evo::app::alog("weather tile: update ConnectedDrive pad -> %s, %s (%s)",
                   weather.city, today.tempMax, today.condition);
}

void finalize() {
    s_cdrApp = nullptr;
    s_lastCity[0] = '\0';
    s_lastTemp[0] = '\0';
}

}  // namespace cdr_weather
}  // namespace evo
