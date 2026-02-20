# Glance
### Daily Agenda for X4

Custom firmware for the [Xteink X4](https://www.xteink.com/) e-ink reader. Syncs calendar events and reminders from an companion app via BLE and displays them on the e-ink screen.

Big thanks to the [CrossPoint project](https://github.com/crosspoint-reader/crosspoint-reader) for their work!

This project is **not affiliated with Xteink**; it's built as a community project.

## Companion app example
There is an open source example app you can build yourself using Xcode 
[Companion App](https://github.com/JayFiDev/Glance-Companion/)


## Features

| Calendar View | Reminders View |
|---------------|-----------|
| ![Calendar view](https://github.com/user-attachments/assets/67736ed6-e708-418d-b0f6-5149d63190b6) | ![ToDo View](https://github.com/user-attachments/assets/b19bfa5b-26c2-4d13-9ace-1c1f80ccdacf) |

- **Three display pages** — Today's Events, Upcoming (3 days), Reminders
- **BLE sync** — Sync calenar and reminder data
- **Reminder management** — Mark reminders complete/incomplete on-device with two-way sync
- **Scrollable lists** — Dynamic row heights, list category subheaders, scrollbar indicator
- **Deep sleep** — Auto-sleep after inactivity, wake via power button


## Setup

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE plugin)

### Clone & Build

```bash
git clone --recurse-submodules https://github.com/<your-username>/glance-daily-agenda.git
cd glance-daily-agenda
```

If you already cloned without `--recurse-submodules`:

```bash
git submodule update --init
```

### Build & Flash

```bash
pio run                  # Build
pio run -t upload        # Flash to device
pio device monitor       # Serial monitor (115200 baud)
```

## Button Mapping

| Button | Position | Action |
|--------|----------|--------|
| BTN_BACK | Bottom left | Previous page |
| BTN_CONFIRM | Bottom center-left | Next page |
| BTN_LEFT | Bottom center-right | Toggle reminder completion |
| BTN_RIGHT | Bottom right | Start/stop BLE sync |
| BTN_UP | Right side top | Scroll up / previous reminder |
| BTN_DOWN | Right side bottom | Scroll down / next reminder |
| BTN_POWER | Top | Long press: deep sleep |

## BLE Sync Protocol

The firmware advertises as `XteinkX4` with a single BLE characteristic that accepts chunked JSON writes from the companion app.

**Receive format** (written by the app):
```json
{
  "syncTime": "2025-01-15T10:30:00+01:00",
  "todayDate": "2025-01-15",
  "upcomingEndDate": "2025-01-18",
  "utcOffsetSeconds": 3600,
  "use24HourTime": true,
  "events": [
    { "title": "Meeting", "start": "...", "end": "...", "allDay": false, "location": "Room A" }
  ],
  "reminders": [
    { "title": "Buy milk", "dueDate": "...", "priority": 1, "completed": false,
      "calendarItemIdentifier": "ABC-123", "list": "Shopping" }
  ]
}
```

**Read value** (completions synced back to the app):
```json
{ "completedIds": ["ABC-123", "DEF-456"] }
```

## Dependencies

- [open-x4-sdk](https://github.com/open-x4-epaper/community-sdk) — Hardware drivers (EInkDisplay, InputManager, BatteryMonitor, SDCardManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) v7.4.2 — JSON parsing (installed automatically by PlatformIO)


---

CrossPoint Reader is **not affiliated with Xteink or any manufacturer of the X4 hardware**.
