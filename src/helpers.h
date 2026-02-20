/*
 * Glance — Helper utilities
 * Date/time manipulation, string helpers, JSON validation.
 */

#pragma once
#include <Arduino.h>

void safeCopy(char* dst, size_t dstSize, const char* src);
void getLocalDate(const char* iso, char* out);
void addDays(const char* date, int days, char* out);
int dateCompare(const char* a, const char* b);
bool isCompleteJSON(const char* data, size_t len);
