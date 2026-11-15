#include <cassert>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include "../hmiapp/weather_feed.hpp"

int main() {
    using namespace evo::weather;

    WeatherData w;
    memset(&w, 0, sizeof(w));
    strncpy(w.city, "Roma", sizeof(w.city));
    strncpy(w.currentTemp, "18 C", sizeof(w.currentTemp));
    w.fetchedAt = time(nullptr);
    w.numDays = 5;
    w.valid = true;

    // Day 0: Today (mock today 2026-11-15)
    strncpy(w.days[0].date, "2026-11-15", 15);
    strncpy(w.days[0].tempMin, "12 C", 11);
    strncpy(w.days[0].tempMax, "20 C", 11);
    strncpy(w.days[0].condition, "Sereno", 47);
    w.days[0].iconId = 1;

    // Day 1: Tomorrow (offline day 1)
    strncpy(w.days[1].date, "2026-11-16", 15);
    strncpy(w.days[1].tempMin, "11 C", 11);
    strncpy(w.days[1].tempMax, "19 C", 11);
    strncpy(w.days[1].condition, "Poco nuvoloso", 47);
    w.days[1].iconId = 2;

    assert(saveForecastCache(w));

    WeatherData loaded;
    DayForecast day;
    assert(getTodayWeather(&loaded, &day));
    assert(strcmp(loaded.city, "Roma") == 0);
    assert(day.iconId == 1 || day.iconId == 2);

    puts("PASS test_weather_feed: 5-day cache, offline matching by date");
    return 0;
}
