// Background synchronisation of calendars (iCal feeds) and weather
// (Open-Meteo). Runs in its own low-priority task on core 0.
#pragma once

namespace feeds {

void startTask();

}  // namespace feeds
