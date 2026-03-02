/*
 * Glance — Daily Agenda for X4
 *
 * Calendar & Reminders display for Xteink X4 e-reader.
 * Receives data from iPhone via BLE, stores on SD, displays on e-ink.
 *
 * Pages: Today's Events | Upcoming (3 days) | Reminders
 * BLE is manually enabled via button press and auto-disables after sync.
 *
 * Based on patterns from the CrossPoint firmware and open-x4-sdk.
 * Hardware: ESP32-C3, 4.26" 800x480 E-Ink (SSD1677), SdFat SD card
 */

#include <Arduino.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <soc/rtc.h>        // rtc_time_get(), rtc_clk_cal() — RTC slow clock runs during deep sleep

#include "globals.h"
#include "helpers.h"
#include "display.h"
#include "bluetooth.h"
#include "storage.h"

// ============================================================================
// GLOBAL VARIABLE DEFINITIONS
// ============================================================================

// Hardware objects
EInkDisplay eink(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
HalDisplay halDisplay(eink);
GfxRenderer renderer(halDisplay);
InputManager input;
BatteryMonitor battery(BAT_GPIO);

// BLE state
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
volatile bool bleConnected = false;
bool bleEnabled = false;
unsigned long bleStartTime = 0;

// BLE receive buffer
char bleBuffer[BLE_BUFFER_SIZE];
size_t bleBufferPos = 0;
volatile bool bleDataReady = false;

// Calendar data
CalendarEvent events[MAX_EVENTS];
int eventCount = 0;
Reminder reminders[MAX_REMINDERS];
int reminderCount = 0;
char lastSyncTime[26] = "";
char todayDate[11] = "";
char upcomingEndDate[11] = "";
bool hasData = false;

// Timezone & time format
int utcOffsetSeconds = 0;
bool use24HourTime = true;

// Display & sleep settings (defaults; overwritten when JSON is parsed)
int autoSleepMinutes = 5;
bool autoSleepEnabled = true;
SleepScreenMode sleepScreenMode = SLEEP_WALLPAPER;
WakeSchedule wakeSchedule = {false, 7, 0};

// RTC memory — survives deep sleep, holds time reference for wake schedule
RTC_DATA_ATTR uint64_t rtcRefUnixTime        = 0; // UTC unix time recorded at rtcRefMillis
RTC_DATA_ATTR uint32_t rtcRefMillis          = 0; // millis() when rtcRefUnixTime was set
RTC_DATA_ATTR uint64_t rtcNextWakeUnixTime   = 0; // target wake unix time (bootstraps timer wake)
RTC_DATA_ATTR bool     rtcWakeScheduleEnabled = false;
RTC_DATA_ATTR uint8_t  rtcWakeHour           = 7;
RTC_DATA_ATTR uint8_t  rtcWakeMinute         = 0;
RTC_DATA_ATTR int32_t  rtcUtcOffsetSec       = 0;
// RTC slow clock tracking — used to measure actual sleep duration on any wake type
RTC_DATA_ATTR uint64_t rtcSleepStartTicks    = 0; // rtc_time_get() snapshot at sleep entry
RTC_DATA_ATTR uint32_t rtcSlowClkCalibration = 0; // us-per-tick * 2^19 (saved before sleep)

// Reminder selection
int selectedReminderIdx = 0;

// Completions (two-way sync)
char completedIds[MAX_COMPLETED_IDS][48];
int completedCount = 0;

// Display state
DisplayMode currentMode = MODE_TODAY;
bool needsDisplayUpdate = false;
bool useFastRefresh = false;
int scrollOffset = 0;
int totalPageItems = 0;
int visiblePageItems = 0;

// Timing
volatile unsigned long lastActivityTime = 0;

// Scheduled wake mode — set true when woken by RTC timer for BLE window
static bool inScheduledWakeMode = false;
static unsigned long scheduledWakeBleStartMs = 0;

// ============================================================================
// POWER MANAGEMENT
// ============================================================================

// Returns seconds until the next scheduled daily wake given a known nowUtc, or 0 if invalid.
// Stores the target time in rtcNextWakeUnixTime for use as a bootstrap reference on timer wake.
static uint64_t calcSecondsUntilWake(uint64_t nowUtc) {
  if (nowUtc == 0) return 0;

  int64_t  localOffset  = (int64_t)rtcUtcOffsetSec;
  uint64_t localNow     = (uint64_t)((int64_t)nowUtc + localOffset);
  uint32_t wakeSecInDay = (uint32_t)rtcWakeHour * 3600u + (uint32_t)rtcWakeMinute * 60u;

  uint64_t todayMidnightLocal = (localNow / 86400ULL) * 86400ULL;
  uint64_t scheduledWakeLocal = todayMidnightLocal + wakeSecInDay;
  uint64_t scheduledWakeUtc   = (uint64_t)((int64_t)scheduledWakeLocal - localOffset);

  if (scheduledWakeUtc <= nowUtc) {
    scheduledWakeUtc += 86400ULL; // already passed today → schedule for tomorrow
  }

  uint64_t sleepSec = scheduledWakeUtc - nowUtc;
  // Sanity check: 1 min – 48 h
  if (sleepSec < 60 || sleepSec > 172800ULL) return 0;

  rtcNextWakeUnixTime = scheduledWakeUtc; // bootstrap reference for the timer wake boot
  return sleepSec;
}

static void enterDeepSleep() {
  if (bleEnabled) stopBLE();

  // Compute "now" from the current reference + elapsed time this boot.
  // Then snapshot it into RTC memory and reset rtcRefMillis to 0 so that
  // after the next wake (millis() restarts at ~0) the calculation is correct.
  uint64_t nowUtc = 0;
  if (rtcRefUnixTime > 0) {
    uint64_t elapsed = (uint64_t)((uint32_t)millis() - rtcRefMillis) / 1000ULL;
    nowUtc = rtcRefUnixTime + elapsed;
    rtcRefUnixTime = nowUtc; // advance to now
    rtcRefMillis   = 0;      // millis() restarts near 0 after wake
  }

  if (Serial) Serial.printf("[PWR] Entering deep sleep. nowUtc=%llu, wake=%d@%02u:%02u, utcOff=%d\n",
                             nowUtc, wakeSchedule.enabled, wakeSchedule.hour,
                             wakeSchedule.minute, utcOffsetSeconds);

  // Always sync wake-schedule config from runtime globals before sleeping,
  // so RTC memory is correct even after a power cycle that cleared it.
  rtcWakeScheduleEnabled = wakeSchedule.enabled;
  rtcWakeHour            = wakeSchedule.hour;
  rtcWakeMinute          = wakeSchedule.minute;
  rtcUtcOffsetSec        = (int32_t)utcOffsetSeconds;

  // Draw the configured sleep screen onto the e-ink panel
  switch (sleepScreenMode) {
    case SLEEP_TODAY:
    case SLEEP_UPCOMING:
    case SLEEP_REMINDERS:
      if (hasData) {
        currentMode  = (sleepScreenMode == SLEEP_TODAY)    ? MODE_TODAY    :
                       (sleepScreenMode == SLEEP_UPCOMING)  ? MODE_UPCOMING : MODE_REMINDERS;
        scrollOffset = 0;
        drawSleepPageScreen();  // draws page + "Sleeping..." footer; bypasses needsDisplayUpdate guard
        break;
      }
      // Fall through to wallpaper if no data loaded
      [[fallthrough]];
    case SLEEP_WALLPAPER:
    default:
      renderer.clearScreen(0xFF);
      if (!loadSleepImage()) {
        drawSleepScreen();
      }
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      break;
  }

  eink.deepSleep();

  input.update();
  while (input.isPressed(InputManager::BTN_POWER)) {
    delay(50);
    input.update();
  }

  // Always allow button-press wake
  esp_deep_sleep_enable_gpio_wakeup(
      1ULL << InputManager::POWER_BUTTON_PIN,
      ESP_GPIO_WAKEUP_GPIO_LOW);

  // Optionally add timer wake for daily BLE window
  if (wakeSchedule.enabled && nowUtc > 0) {
    uint64_t sleepSec = calcSecondsUntilWake(nowUtc); // pass computed nowUtc, not re-derive
    if (sleepSec > 0) {
      if (Serial) Serial.printf("[PWR] Timer wake in %llus (%uh %02um)\n",
                                sleepSec, (unsigned)(sleepSec / 3600),
                                (unsigned)((sleepSec % 3600) / 60));
      esp_sleep_enable_timer_wakeup(sleepSec * 1000000ULL);
    } else {
      if (Serial) Serial.println("[PWR] Wake schedule: could not compute valid sleep duration");
    }
  }

  // Snapshot the RTC slow clock so setup() can compute exact sleep duration on any wake type.
  // rtc_time_get() keeps counting during deep sleep; millis() resets to 0 on every wake.
  rtcSlowClkCalibration = rtc_clk_cal(RTC_CAL_RTC_MUX, 100); // ~1 ms; saves cal so wake needs no recal
  rtcSleepStartTicks    = rtc_time_get();

  if (Serial) {
    Serial.println("[PWR] Good night!");
    delay(100);
  }

  esp_deep_sleep_start();
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  pinMode(USB_PIN, INPUT);
  if (digitalRead(USB_PIN) == HIGH) {
    Serial.begin(115200);
    unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000) delay(10);
  }

  if (Serial) {
    Serial.println("================================");
    Serial.println("  Glance " FIRMWARE_VERSION);
    Serial.println("================================");
  }

  input.begin();
  if (Serial) Serial.println("[INIT] Buttons ready");

  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI);
  if (Serial) Serial.println("[INIT] SPI ready");

  if (!SDCard.begin()) {
    if (Serial) Serial.println("[INIT] SD card failed!");
  } else {
    if (Serial) Serial.println("[INIT] SD card ready");
  }

  halDisplay.begin();
  renderer.begin();
  if (Serial) Serial.println("[INIT] Display ready");

  initFonts();
  if (Serial) Serial.println("[INIT] Fonts registered");

  renderer.clearScreen(0xFF);
  drawBootScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  if (loadFromSD()) {
    if (Serial) Serial.println("[INIT] Calendar data loaded");
  }
  loadCompletionsFromSD();

  // Check if this boot is a scheduled timer wake for the BLE window.
  // Must be read before any code that might alter wakeup state.
  esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();

  if (wakeupCause == ESP_SLEEP_WAKEUP_TIMER && rtcWakeScheduleEnabled) {
    // Timer wake: use the exact stored target time — no RTC tick math needed.
    if (Serial) Serial.println("[PWR] Scheduled BLE wake — opening sync window");
    if (rtcNextWakeUnixTime > 0) {
      rtcRefUnixTime = rtcNextWakeUnixTime;
    }
    rtcRefMillis       = 0;
    rtcSleepStartTicks = 0; // mark as consumed
    inScheduledWakeMode     = true;
    scheduledWakeBleStartMs = millis();
    startBLE();
  } else {
    // GPIO (button) wake or power-on boot.
    // millis() reset to 0 on wake, so we must add the actual deep-sleep duration using
    // the RTC slow clock (which keeps counting during sleep).
    if (rtcRefUnixTime > 0 && rtcSleepStartTicks > 0 && rtcSlowClkCalibration > 0) {
      uint64_t sleepTicks = rtc_time_get() - rtcSleepStartTicks;
      uint64_t sleepSec   = ((sleepTicks * (uint64_t)rtcSlowClkCalibration) >> 19) / 1000000ULL;
      if (sleepSec > 0 && sleepSec < 172800ULL) {
        rtcRefUnixTime += sleepSec;
        if (Serial) Serial.printf("[PWR] Button wake, slept %llus\n", sleepSec);
      }
    }
    rtcRefMillis       = (uint32_t)millis();
    rtcSleepStartTicks = 0;

    // If still no time reference (power cycle cleared RTC memory), seed from SD.
    if (rtcRefUnixTime == 0 && strlen(lastSyncTime) >= 19) {
      rtcRefUnixTime = isoToUnixTime(lastSyncTime);
      rtcRefMillis   = (uint32_t)millis();
      if (Serial) Serial.printf("[PWR] Seeded time ref from SD: unix=%llu\n", rtcRefUnixTime);
    }
    if (Serial) Serial.println("[INIT] BLE is OFF (press Sync to enable)");
  }

  needsDisplayUpdate = true;
  lastActivityTime = millis();

  if (Serial) {
    Serial.println("================================");
    Serial.printf("[MEM] Free heap: %d bytes\n", ESP.getFreeHeap());
  }

  updateDisplay();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  input.update();

  // --- Power button: long press -> sleep ---
  if (input.isPressed(InputManager::BTN_POWER) &&
      input.getHeldTime() > POWER_BTN_SLEEP_MS) {
    enterDeepSleep();
    return;
  }

  // --- Button 1 (BTN_BACK, leftmost): cycle pages ← ---
  if (input.wasPressed(InputManager::BTN_BACK) && hasData && !bleEnabled) {
    currentMode = static_cast<DisplayMode>((currentMode + MODE_COUNT - 1) % MODE_COUNT);
    scrollOffset = 0;
    selectedReminderIdx = 0;
    needsDisplayUpdate = true;
    lastActivityTime = millis();
    if (Serial) Serial.printf("[UI] Page: %s\n", MODE_TITLES[currentMode]);
  }

  // --- Button 2 (BTN_CONFIRM): cycle pages → ---
  if (input.wasPressed(InputManager::BTN_CONFIRM) && hasData && !bleEnabled) {
    currentMode = static_cast<DisplayMode>((currentMode + 1) % MODE_COUNT);
    scrollOffset = 0;
    selectedReminderIdx = 0;
    needsDisplayUpdate = true;
    lastActivityTime = millis();
    if (Serial) Serial.printf("[UI] Page: %s\n", MODE_TITLES[currentMode]);
  }

  // --- Button 3 (BTN_LEFT): toggle reminder completion ---
  if (input.wasPressed(InputManager::BTN_LEFT) && hasData && !bleEnabled) {
    if (currentMode == MODE_REMINDERS && reminderCount > 0 &&
        selectedReminderIdx >= 0 && selectedReminderIdx < reminderCount) {
      Reminder& rem = reminders[selectedReminderIdx];
      rem.completed = !rem.completed;

      if (rem.completed && rem.calendarItemIdentifier[0] != '\0') {
        // Toggled TO completed: record the ID for two-way sync
        if (completedCount < MAX_COMPLETED_IDS) {
          safeCopy(completedIds[completedCount], 48, rem.calendarItemIdentifier);
          completedCount++;
          saveCompletionsToSD();
        }
      } else if (!rem.completed && rem.calendarItemIdentifier[0] != '\0') {
        // Toggled TO incomplete: remove from completedIds
        for (int i = 0; i < completedCount; i++) {
          if (strncmp(completedIds[i], rem.calendarItemIdentifier, 48) == 0) {
            for (int j = i; j < completedCount - 1; j++) {
              memcpy(completedIds[j], completedIds[j + 1], 48);
            }
            completedCount--;
            saveCompletionsToSD();
            break;
          }
        }
      }

      needsDisplayUpdate = true;
      useFastRefresh = true;
      lastActivityTime = millis();
      if (Serial) Serial.printf("[UI] Toggled reminder %d: %s\n",
                                 selectedReminderIdx, rem.completed ? "completed" : "uncompleted");
    }
  }

  // --- Button 4 (BTN_RIGHT): toggle BLE sync ---
  if (input.wasPressed(InputManager::BTN_RIGHT)) {
    lastActivityTime = millis();
    if (bleEnabled) {
      stopBLE();
      if (Serial) Serial.println("[UI] BLE sync cancelled");
    } else {
      startBLE();
      if (Serial) Serial.println("[UI] BLE sync started");
    }
  }

  // --- Side buttons: scroll / selection (use fast refresh) ---
  if (input.wasPressed(InputManager::BTN_UP)) {
    if (Serial) Serial.printf("[UI] BTN_UP pressed (hasData=%d ble=%d mode=%d rem=%d sel=%d)\n",
                               hasData, bleEnabled, currentMode, reminderCount, selectedReminderIdx);
    if (hasData && !bleEnabled) {
      if (currentMode == MODE_REMINDERS && reminderCount > 0) {
        if (selectedReminderIdx > 0) {
          selectedReminderIdx--;
          if (selectedReminderIdx < scrollOffset) {
            scrollOffset = selectedReminderIdx;
          }
          needsDisplayUpdate = true;
          useFastRefresh = true;
          lastActivityTime = millis();
        }
      } else {
        if (scrollOffset > 0) {
          scrollOffset--;
          needsDisplayUpdate = true;
          useFastRefresh = true;
          lastActivityTime = millis();
        }
      }
    }
  }

  if (input.wasPressed(InputManager::BTN_DOWN)) {
    if (Serial) Serial.printf("[UI] BTN_DOWN pressed (hasData=%d ble=%d mode=%d rem=%d sel=%d)\n",
                               hasData, bleEnabled, currentMode, reminderCount, selectedReminderIdx);
    if (hasData && !bleEnabled) {
      if (currentMode == MODE_REMINDERS && reminderCount > 0) {
        if (selectedReminderIdx < reminderCount - 1) {
          selectedReminderIdx++;
          if (selectedReminderIdx >= scrollOffset + visiblePageItems) {
            scrollOffset = selectedReminderIdx - visiblePageItems + 1;
          }
          needsDisplayUpdate = true;
          useFastRefresh = true;
          lastActivityTime = millis();
        }
      } else {
        if (scrollOffset + visiblePageItems < totalPageItems) {
          scrollOffset++;
          needsDisplayUpdate = true;
          useFastRefresh = true;
          lastActivityTime = millis();
        }
      }
    }
  }

  // --- Track button activity ---
  if (input.wasAnyPressed() || input.wasAnyReleased()) {
    lastActivityTime = millis();
  }

  // --- Process received BLE data ---
  if (bleDataReady) {
    if (Serial) Serial.println("[MAIN] Processing BLE data...");
    saveToSD(bleBuffer, bleBufferPos);
    parseCalendarJSON(bleBuffer, bleBufferPos);
    clearCompletions();

    // Refresh RTC time reference using the new sync timestamp
    if (strlen(lastSyncTime) >= 19) {
      rtcRefUnixTime         = isoToUnixTime(lastSyncTime);
      rtcRefMillis           = (uint32_t)millis();
      rtcWakeScheduleEnabled = wakeSchedule.enabled;
      rtcWakeHour            = wakeSchedule.hour;
      rtcWakeMinute          = wakeSchedule.minute;
      rtcUtcOffsetSec        = (int32_t)utcOffsetSeconds;
      if (Serial) Serial.printf("[PWR] RTC ref updated: unix=%llu wake=%02u:%02u enabled=%d\n",
                                rtcRefUnixTime, rtcWakeHour, rtcWakeMinute, rtcWakeScheduleEnabled);
    }

    bleBufferPos = 0;
    bleBuffer[0] = '\0';
    bleDataReady = false;

    stopBLE();
    needsDisplayUpdate = true;

    if (Serial) Serial.println("[MAIN] Sync complete, BLE disabled");

    // In scheduled wake mode: sync done → go back to sleep
    if (inScheduledWakeMode) {
      if (Serial) Serial.println("[PWR] Scheduled sync complete, returning to sleep");
      delay(500); // brief pause so display can update
      enterDeepSleep();
      return;
    }
  }

  // --- BLE advertising timeout ---
  if (bleEnabled && !bleConnected &&
      millis() - bleStartTime >= BLE_TIMEOUT_MS) {
    if (Serial) Serial.println("[BLE] Advertising timeout, disabling");
    stopBLE();
    // In scheduled wake mode with no connection: go back to sleep
    if (inScheduledWakeMode) {
      if (Serial) Serial.println("[PWR] Scheduled BLE window expired, returning to sleep");
      enterDeepSleep();
      return;
    }
  }

  // --- Scheduled wake mode: 10-min hard limit ---
  if (inScheduledWakeMode &&
      millis() - scheduledWakeBleStartMs >= BLE_WAKE_DURATION_MS) {
    if (Serial) Serial.println("[PWR] Scheduled BLE wake window closed");
    if (bleEnabled) stopBLE();
    enterDeepSleep();
    return;
  }

  // --- Update display ---
  if (needsDisplayUpdate) {
    updateDisplay();
  }

  // --- Auto-sleep (not during a scheduled BLE wake window) ---
  if (autoSleepEnabled && !inScheduledWakeMode &&
      millis() - lastActivityTime >= (unsigned long)autoSleepMinutes * 60UL * 1000UL) {
    if (Serial) Serial.println("[PWR] Auto-sleep timeout");
    enterDeepSleep();
    return;
  }

  // --- Power-efficient delay ---
  if (millis() - lastActivityTime >= IDLE_THRESHOLD_MS) {
    delay(IDLE_DELAY_MS);
  } else {
    delay(ACTIVE_DELAY_MS);
  }

  // --- Periodic memory log ---
  static unsigned long lastMemLog = 0;
  if (Serial && millis() - lastMemLog >= 30000) {
    Serial.printf("[MEM] Free: %d bytes\n", ESP.getFreeHeap());
    lastMemLog = millis();
  }
}
