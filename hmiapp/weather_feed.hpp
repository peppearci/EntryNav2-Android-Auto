#pragma once

#include <stdint.h>
#include <time.h>

namespace evo {
namespace weather {

// 5 days of forecast: day 0 = today, day 1..4 = next days
static const int kMaxDays = 5;

struct DayForecast {
    char date[16];        // "YYYY-MM-DD"
    char tempMin[12];     // "12°C"
    char tempMax[12];     // "20°C"
    char condition[48];   // "Sereno", "Nuvoloso", etc.
    uint32_t iconId;
};

struct WeatherData {
    char city[48];
    char currentTemp[12]; // "18°C"
    DayForecast days[kMaxDays];
    int numDays;
    time_t fetchedAt;
    bool valid;
};

// Returns forecast matching today's date from local cache if offline,
// or current live day if available.
bool getTodayWeather(WeatherData* out, DayForecast* outDay);

// Save multi-day forecast to persistent disk cache
bool saveForecastCache(const WeatherData& in);

// Load multi-day forecast from persistent disk cache
bool loadForecastCache(WeatherData* out);

}  // namespace weather
}  // namespace evo
