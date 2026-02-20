/*
 * Glance — Global state declarations
 * Extern declarations for shared variables.
 * Actual definitions live in main.cpp.
 */

#pragma once
#include "config.h"

#include <EInkDisplay.h>
#include <HalDisplay.h>
#include <GfxRenderer.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <SDCardManager.h>

// Forward declarations for BLE types (full headers only in bluetooth.cpp)
class BLEServer;
class BLECharacteristic;

#define SDCard SDCardManager::getInstance()

// ============================================================================
// HARDWARE OBJECTS
// ============================================================================

extern EInkDisplay eink;
extern HalDisplay halDisplay;
extern GfxRenderer renderer;
extern InputManager input;
extern BatteryMonitor battery;

// ============================================================================
// GLOBAL STATE
// ============================================================================

// BLE state
extern BLEServer* pServer;
extern BLECharacteristic* pCharacteristic;
extern volatile bool bleConnected;
extern bool bleEnabled;
extern unsigned long bleStartTime;

// BLE buffer
extern char bleBuffer[];
extern size_t bleBufferPos;
extern volatile bool bleDataReady;

// Calendar data
extern CalendarEvent events[];
extern int eventCount;
extern Reminder reminders[];
extern int reminderCount;
extern char lastSyncTime[26];
extern char todayDate[11];
extern char upcomingEndDate[11];
extern bool hasData;

// Timezone & time format
extern int utcOffsetSeconds;
extern bool use24HourTime;

// Reminder selection
extern int selectedReminderIdx;

// Completions (two-way sync)
extern char completedIds[][48];
extern int completedCount;

// Display state
extern DisplayMode currentMode;
extern bool needsDisplayUpdate;
extern bool useFastRefresh;
extern int scrollOffset;
extern int totalPageItems;
extern int visiblePageItems;

// Timing
extern volatile unsigned long lastActivityTime;

