/*
 * Glance — Shared configuration
 * Constants, pin definitions, data structures, display modes.
 */

#pragma once
#include <Arduino.h>

// ============================================================================
// HARDWARE PINS (from open-x4-sdk / CrossPoint)
// ============================================================================

#define EPD_SCLK  8
#define EPD_MOSI  10
#define EPD_CS    21
#define EPD_DC    4
#define EPD_RST   5
#define EPD_BUSY  6
#define SPI_MISO  7
#define BAT_GPIO  0
#define USB_PIN   20

// ============================================================================
// BLE CONFIGURATION
// ============================================================================

#define SERVICE_UUID        "12345678-1234-1234-1234-123456789012"
#define CHARACTERISTIC_UUID "87654321-4321-4321-4321-210987654321"
#define DEVICE_NAME         "XteinkX4"

// ============================================================================
// CONSTANTS
// ============================================================================

// Layout metrics — tuned for NotoSans 14pt/12pt
static constexpr int CONTENT_SIDE_PADDING = 20;
static constexpr int HEADER_HEIGHT = 50;
static constexpr int BATTERY_WIDTH = 26;
static constexpr int BATTERY_HEIGHT = 20;
static constexpr int BUTTON_HINTS_HEIGHT = 48;
static constexpr int FOOTER_INFO_HEIGHT = 22;
static constexpr int EVENT_ROW_HEIGHT = 80;

// Button hint positions (from CrossPoint — 4 physical buttons L→R)
// Physical: BTN_BACK, BTN_CONFIRM, BTN_LEFT, BTN_RIGHT
static constexpr int BUTTON_WIDTH = 106;
static constexpr int BUTTON_POSITIONS[] = {25, 130, 245, 350};

// Data limits
static constexpr int MAX_EVENTS = 20;
static constexpr int MAX_REMINDERS = 60;
static constexpr int MAX_COMPLETED_IDS = MAX_REMINDERS;

// BLE receive buffer
static constexpr size_t BLE_BUFFER_SIZE = 24576;

// Timing
static constexpr unsigned long SLEEP_TIMEOUT_MS     = 10UL * 60 * 1000;
static constexpr unsigned long BLE_TIMEOUT_MS       = 2UL * 60 * 1000;
static constexpr unsigned long IDLE_THRESHOLD_MS    = 3000;
static constexpr unsigned long IDLE_DELAY_MS        = 50;
static constexpr unsigned long ACTIVE_DELAY_MS      = 10;
static constexpr uint16_t     POWER_BTN_SLEEP_MS    = 1000;

// ============================================================================
// DISPLAY MODES
// ============================================================================

enum DisplayMode : uint8_t {
  MODE_TODAY = 0,
  MODE_UPCOMING = 1,
  MODE_REMINDERS = 2,
  MODE_COUNT = 3
};

inline const char* const MODE_TITLES[] = {"Today", "Upcoming", "Reminders"};

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct CalendarEvent {
  char title[64];
  char start[26];
  char end[26];
  char location[64];
  bool allDay;
};

struct Reminder {
  char title[64];
  char dueDate[26];
  char calendarItemIdentifier[48];
  char list[32];
  uint8_t priority;
  bool completed;
};
