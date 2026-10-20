// French date/time formatting helpers.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../core/tz.h"
#include "app.h"

namespace fmt {

LocalTime localTime(const PosixTz& tz, int64_t utc);

const char* dayName(int wday);    // "lundi" (0 = Monday)
const char* dayShort(int wday);   // "lun."
const char* monthName(int m);     // "octobre" (1..12)
const char* monthShort(int m);    // "oct."

void hhmm(char* out, size_t n, const PosixTz& tz, int64_t utc);  // "09:05"
void mmss(char* out, size_t n, uint32_t ms);                      // "3:07"
// "dans 25 min", "dans 1 h 05", "dans 3 j"
void until(char* out, size_t n, int64_t seconds);
// "Aujourd'hui", "Demain", "Hier" or "Jeudi 22 octobre"
void dayLabel(char* out, size_t n, int64_t day, int64_t today);
// "Mardi 20 octobre"
void longDate(char* out, size_t n, const LocalTime& lt);
// Upper-cases the first ASCII letter in place.
void capitalize(char* s);

}  // namespace fmt
