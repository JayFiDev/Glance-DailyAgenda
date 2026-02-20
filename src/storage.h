/*
 * Glance — Storage module
 * JSON parsing, SD card read/write, completions persistence.
 */

#pragma once
#include <Arduino.h>

void parseCalendarJSON(const char* json, size_t len);
bool saveToSD(const char* data, size_t len);
bool loadFromSD();

void saveCompletionsToSD();
void loadCompletionsFromSD();
void clearCompletions();
