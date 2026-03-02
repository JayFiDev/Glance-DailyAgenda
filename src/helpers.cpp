/*
 * Glance — Helper utilities implementation
 * Date/time manipulation, string helpers, JSON validation.
 */

#include "helpers.h"
#include "globals.h"

void safeCopy(char* dst, size_t dstSize, const char* src) {
  if (!src) { dst[0] = '\0'; return; }
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

// Add N days to "YYYY-MM-DD" and write result to out (must be at least 11 bytes)
void addDays(const char* date, int days, char* out) {
  int y = (date[0]-'0')*1000 + (date[1]-'0')*100 + (date[2]-'0')*10 + (date[3]-'0');
  int m = (date[5]-'0')*10 + (date[6]-'0');
  int d = (date[8]-'0')*10 + (date[9]-'0');
  static const int dim[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  d += days;
  for (;;) {
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    int maxD = dim[m] + (m == 2 && leap ? 1 : 0);
    if (d <= maxD) break;
    d -= maxD;
    m++;
    if (m > 12) { m = 1; y++; }
  }
  snprintf(out, 11, "%04d-%02d-%02d", y, m, d);
}

// Get timezone-adjusted local date from an ISO datetime string.
void getLocalDate(const char* iso, char* out) {
  if (strlen(iso) < 10) { out[0] = '\0'; return; }
  strncpy(out, iso, 10);
  out[10] = '\0';

  if (utcOffsetSeconds == 0) return;

  const char* t = strchr(iso, 'T');
  if (!t || strlen(t) < 6) return;

  int hour = (t[1] - '0') * 10 + (t[2] - '0');
  int minute = (t[4] - '0') * 10 + (t[5] - '0');
  int totalMinutes = hour * 60 + minute + (utcOffsetSeconds / 60);

  if (totalMinutes >= 1440) {
    char tmp[11];
    addDays(out, 1, tmp);
    memcpy(out, tmp, 11);
  } else if (totalMinutes < 0) {
    int y = (out[0]-'0')*1000 + (out[1]-'0')*100 + (out[2]-'0')*10 + (out[3]-'0');
    int m = (out[5]-'0')*10 + (out[6]-'0');
    int d = (out[8]-'0')*10 + (out[9]-'0');
    d--;
    if (d < 1) {
      m--;
      if (m < 1) { m = 12; y--; }
      static const int dim[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
      d = dim[m] + (m == 2 && leap ? 1 : 0);
    }
    snprintf(out, 11, "%04d-%02d-%02d", y, m, d);
  }
}

// Compare two ISO date/datetime strings by their timezone-adjusted local date
int dateCompare(const char* a, const char* b) {
  char aLocal[11], bLocal[11];
  getLocalDate(a, aLocal);
  getLocalDate(b, bLocal);
  return strncmp(aLocal, bLocal, 10);
}

// Converts "YYYY-MM-DDTHH:MM:SS..." (UTC) to a Unix timestamp (seconds since 1970-01-01).
uint64_t isoToUnixTime(const char* iso) {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 6) {
    return 0;
  }
  if (year < 2020 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) {
    return 0;
  }

  // Cumulative days before each month (non-leap year)
  static const uint16_t daysBeforeMonth[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

  uint64_t days = 0;
  for (int y = 1970; y < year; y++) {
    days += ((y % 4 == 0) && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
  }
  days += daysBeforeMonth[month - 1];
  // Leap year correction: add a day if we're past February
  if (month > 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
    days += 1;
  }
  days += (uint64_t)(day - 1);

  return days * 86400ULL
       + (uint64_t)hour   * 3600ULL
       + (uint64_t)minute * 60ULL
       + (uint64_t)second;
}

bool isCompleteJSON(const char* data, size_t len) {
  if (len == 0) return false;
  int braces = 0;
  bool found = false;
  bool inString = false;
  bool escaped = false;
  for (size_t i = 0; i < len; i++) {
    char c = data[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (c == '\\' && inString) {
      escaped = true;
      continue;
    }
    if (c == '"') {
      inString = !inString;
      continue;
    }
    if (!inString) {
      if (c == '{') { braces++; found = true; }
      if (c == '}') braces--;
    }
  }
  return found && braces == 0;
}
