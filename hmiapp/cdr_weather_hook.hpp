#pragma once

#include "weather_feed.hpp"

namespace evo {
namespace cdr_weather {

// Installs / updates ConnectedDrive tile dynamic text & icon
void update(const weather::WeatherData& weather, const weather::DayForecast& today);

// Clean up hook when app finalizes
void finalize();

}  // namespace cdr_weather
}  // namespace evo
