/*
 * Glance — Storage module implementation
 * JSON parsing, SD card read/write, completions persistence.
 */

#include "storage.h"
#include "globals.h"
#include "helpers.h"

#include <ArduinoJson.h>

// ============================================================================
// JSON PARSING
// ============================================================================

void parseCalendarJSON(const char* json, size_t len) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json, len);

  if (error) {
    if (Serial) Serial.printf("[JSON] Parse error: %s\n", error.c_str());
    return;
  }

  eventCount = 0;
  reminderCount = 0;

  JsonArray eventsArray;
  JsonArray remindersArray;
  int version = doc["version"] | 1;

  if (version >= 2 && doc["calendarData"].is<JsonObject>()) {
    JsonObject calData = doc["calendarData"];
    safeCopy(lastSyncTime, sizeof(lastSyncTime), calData["syncDate"] | "");
    eventsArray = calData["events"];
    remindersArray = calData["reminders"];

    // Parse display config
    if (doc["displayConfig"].is<JsonObject>()) {
      JsonObject config = doc["displayConfig"];
      utcOffsetSeconds    = config["utcOffsetSeconds"] | 0;
      use24HourTime       = config["use24HourTime"] | true;
      autoSleepEnabled    = config["autoSleepEnabled"] | true;
      autoSleepMinutes    = config["autoSleepMinutes"] | 5;
      int rawScreen       = config["sleepScreen"] | 0;
      sleepScreenMode     = (SleepScreenMode)rawScreen;
      wakeSchedule.enabled = config["wakeScheduleEnabled"] | false;
      wakeSchedule.hour    = config["wakeScheduleHour"] | 7;
      wakeSchedule.minute  = config["wakeScheduleMinute"] | 0;
    }
  } else {
    safeCopy(lastSyncTime, sizeof(lastSyncTime), doc["syncDate"] | "");
    eventsArray = doc["events"];
    remindersArray = doc["reminders"];
  }

  // Derive today's date and upcoming window from syncDate (timezone-adjusted)
  if (strlen(lastSyncTime) >= 10) {
    getLocalDate(lastSyncTime, todayDate);
    addDays(todayDate, 3, upcomingEndDate);
  }

  for (JsonObject evt : eventsArray) {
    if (eventCount >= MAX_EVENTS) break;
    CalendarEvent& e = events[eventCount];
    safeCopy(e.title, sizeof(e.title), evt["title"] | "");
    safeCopy(e.start, sizeof(e.start), evt["start"] | "");
    safeCopy(e.end, sizeof(e.end), evt["end"] | "");
    safeCopy(e.location, sizeof(e.location), evt["location"] | "");
    e.allDay = evt["allDay"] | false;
    eventCount++;
  }

  for (JsonObject rem : remindersArray) {
    if (reminderCount >= MAX_REMINDERS) break;
    Reminder& r = reminders[reminderCount];
    safeCopy(r.title, sizeof(r.title), rem["title"] | "");
    safeCopy(r.dueDate, sizeof(r.dueDate), rem["dueDate"] | "");
    safeCopy(r.calendarItemIdentifier, sizeof(r.calendarItemIdentifier),
             rem["calendarItemIdentifier"] | "");
    safeCopy(r.list, sizeof(r.list), rem["list"] | "");
    r.priority = rem["priority"] | 0;
    r.completed = rem["completed"] | false;
    reminderCount++;
  }

  hasData = (eventCount > 0 || reminderCount > 0);
  currentMode = MODE_TODAY;
  selectedReminderIdx = 0;

  if (Serial)
    Serial.printf("[JSON] Parsed: %d events, %d reminders, today=%s\n",
                  eventCount, reminderCount, todayDate);
}

// ============================================================================
// SD CARD OPERATIONS
// ============================================================================

bool saveToSD(const char* data, size_t len) {
  if (!SDCard.ready()) {
    if (Serial) Serial.println("[SD] Card not ready");
    return false;
  }

  FsFile file;
  if (!SDCard.openFileForWrite("CAL", "/calendar.json", file)) {
    if (Serial) Serial.println("[SD] Failed to open for write");
    return false;
  }

  size_t written = file.write(data, len);
  file.close();
  if (written != len) {
    if (Serial) Serial.printf("[SD] Write error: %d of %d bytes\n", (int)written, (int)len);
    return false;
  }
  if (Serial) Serial.printf("[SD] Saved %d bytes\n", (int)len);
  return true;
}

bool loadFromSD() {
  if (!SDCard.ready()) {
    if (Serial) Serial.println("[SD] Card not ready");
    return false;
  }

  if (!SDCard.exists("/calendar.json")) {
    if (Serial) Serial.println("[SD] No calendar.json found");
    return false;
  }

  // Note: reuses bleBuffer as scratch space (safe because BLE is not active during boot)
  size_t bytesRead = SDCard.readFileToBuffer(
      "/calendar.json", bleBuffer, BLE_BUFFER_SIZE);

  if (bytesRead == 0) {
    if (Serial) Serial.println("[SD] Empty or failed to read");
    return false;
  }

  if (Serial) Serial.printf("[SD] Loaded %d bytes\n", (int)bytesRead);
  parseCalendarJSON(bleBuffer, bytesRead);

  bleBufferPos = 0;
  bleBuffer[0] = '\0';
  return true;
}

// ============================================================================
// COMPLETIONS PERSISTENCE (Two-Way Sync)
// ============================================================================

void saveCompletionsToSD() {
  if (!SDCard.ready()) return;

  JsonDocument doc;
  JsonArray arr = doc["completedIds"].to<JsonArray>();
  for (int i = 0; i < completedCount; i++) {
    arr.add(completedIds[i]);
  }

  char buf[512];
  size_t len = serializeJson(doc, buf, sizeof(buf));

  FsFile file;
  if (SDCard.openFileForWrite("CMP", "/completions.json", file)) {
    size_t written = file.write(buf, len);
    file.close();
    if (written != len) {
      if (Serial) Serial.println("[SD] Completions write error");
    } else {
      if (Serial) Serial.printf("[SD] Saved %d completions\n", completedCount);
    }
  }
}

void loadCompletionsFromSD() {
  if (!SDCard.ready() || !SDCard.exists("/completions.json")) return;

  char buf[512];
  size_t bytesRead = SDCard.readFileToBuffer("/completions.json", buf, sizeof(buf));
  if (bytesRead == 0) return;

  JsonDocument doc;
  if (deserializeJson(doc, buf, bytesRead)) return;

  completedCount = 0;
  JsonArray arr = doc["completedIds"];
  for (JsonVariant v : arr) {
    if (completedCount >= MAX_COMPLETED_IDS) break;
    safeCopy(completedIds[completedCount], 48, v.as<const char*>());
    completedCount++;
  }
  if (Serial) Serial.printf("[SD] Loaded %d completions\n", completedCount);
}

void clearCompletions() {
  completedCount = 0;
  if (SDCard.ready() && SDCard.exists("/completions.json")) {
    SDCard.remove("/completions.json");
  }
  if (Serial) Serial.println("[SD] Cleared completions");
}
