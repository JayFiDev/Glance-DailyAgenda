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

// ============================================================================
// POWER MANAGEMENT
// ============================================================================

static void enterDeepSleep() {
  if (Serial) Serial.println("[PWR] Entering deep sleep...");

  if (bleEnabled) stopBLE();

  renderer.clearScreen(0xFF);
  if (!loadSleepImage()) {
    drawSleepScreen();
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  eink.deepSleep();

  input.update();
  while (input.isPressed(InputManager::BTN_POWER)) {
    delay(50);
    input.update();
  }

  esp_deep_sleep_enable_gpio_wakeup(
      1ULL << InputManager::POWER_BUTTON_PIN,
      ESP_GPIO_WAKEUP_GPIO_LOW);

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

  if (Serial) Serial.println("[INIT] BLE is OFF (press Sync to enable)");

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

    bleBufferPos = 0;
    bleBuffer[0] = '\0';
    bleDataReady = false;

    stopBLE();
    needsDisplayUpdate = true;

    if (Serial) Serial.println("[MAIN] Sync complete, BLE disabled");
  }

  // --- BLE advertising timeout ---
  if (bleEnabled && !bleConnected &&
      millis() - bleStartTime >= BLE_TIMEOUT_MS) {
    if (Serial) Serial.println("[BLE] Advertising timeout, disabling");
    stopBLE();
  }

  // --- Update display ---
  if (needsDisplayUpdate) {
    updateDisplay();
  }

  // --- Auto-sleep ---
  if (millis() - lastActivityTime >= SLEEP_TIMEOUT_MS) {
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
