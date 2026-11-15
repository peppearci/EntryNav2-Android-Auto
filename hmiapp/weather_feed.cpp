#include "weather_feed.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

namespace evo {
namespace weather {

static const char* kCacheFile = "/tmp/weather_cache.txt";
static const char* kPersistFile = "/var/opt/weather_cache.txt";

static void getTodayDateStr(char* buf, size_t sz) {
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, sz, "%Y-%m-%d", &tm_now);
}

bool saveForecastCache(const WeatherData& in) {
    if (!in.valid || in.numDays <= 0) return false;

    // Try persistent first, fallback to /tmp
    FILE* f = fopen(kPersistFile, "w");
    if (!f) {
        f = fopen(kCacheFile, "w");
    }
    if (!f) return false;

    fprintf(f, "%s\n%s\n%ld\n%d\n", in.city, in.currentTemp, static_cast<long>(in.fetchedAt), in.numDays);
    for (int i = 0; i < in.numDays && i < kMaxDays; ++i) {
        fprintf(f, "%s|%s|%s|%s|%u\n",
                in.days[i].date,
                in.days[i].tempMin,
                in.days[i].tempMax,
                in.days[i].condition,
                in.days[i].iconId);
    }
    fclose(f);
    return true;
}

bool loadForecastCache(WeatherData* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    FILE* f = fopen(kPersistFile, "r");
    if (!f) {
        f = fopen(kCacheFile, "r");
    }
    if (!f) return false;

    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return false; }
    line[strcspn(line, "\r\n")] = '\0';
    strncpy(out->city, line, sizeof(out->city) - 1);

    if (!fgets(line, sizeof(line), f)) { fclose(f); return false; }
    line[strcspn(line, "\r\n")] = '\0';
    strncpy(out->currentTemp, line, sizeof(out->currentTemp) - 1);

    if (!fgets(line, sizeof(line), f)) { fclose(f); return false; }
    out->fetchedAt = static_cast<time_t>(strtol(line, nullptr, 10));

    if (!fgets(line, sizeof(line), f)) { fclose(f); return false; }
    out->numDays = atoi(line);
    if (out->numDays > kMaxDays) out->numDays = kMaxDays;

    for (int i = 0; i < out->numDays; ++i) {
        if (!fgets(line, sizeof(line), f)) break;
        line[strcspn(line, "\r\n")] = '\0';
        char* token = strtok(line, "|");
        if (token) strncpy(out->days[i].date, token, sizeof(out->days[i].date) - 1);
        token = strtok(nullptr, "|");
        if (token) strncpy(out->days[i].tempMin, token, sizeof(out->days[i].tempMin) - 1);
        token = strtok(nullptr, "|");
        if (token) strncpy(out->days[i].tempMax, token, sizeof(out->days[i].tempMax) - 1);
        token = strtok(nullptr, "|");
        if (token) strncpy(out->days[i].condition, token, sizeof(out->days[i].condition) - 1);
        token = strtok(nullptr, "|");
        if (token) out->days[i].iconId = static_cast<uint32_t>(strtoul(token, nullptr, 10));
    }
    fclose(f);
    out->valid = (out->city[0] != '\0' && out->numDays > 0);
    return out->valid;
}

bool getTodayWeather(WeatherData* out, DayForecast* outDay) {
    if (!loadForecastCache(out) || !outDay) return false;

    char todayStr[16];
    getTodayDateStr(todayStr, sizeof(todayStr));

    // Match today's date in cached days (handles offline days seamlessly!)
    for (int i = 0; i < out->numDays; ++i) {
        if (strcmp(out->days[i].date, todayStr) == 0) {
            *outDay = out->days[i];
            return true;
        }
    }

    // shortcut: fallback to day 0 if recent cache exists
    if (out->numDays > 0) {
        *outDay = out->days[0];
        return true;
    }
    return false;
}

}  // namespace weather
}  // namespace evo
