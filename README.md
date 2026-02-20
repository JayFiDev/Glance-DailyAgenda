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

### Receive format (written by the app)

```json
{
  "version": 2,
  "syncDate": "2025-01-15T10:30:00+01:00",
  "displayConfig": {
    "utcOffsetSeconds": 3600,
    "use24HourTime": true
  },
  "calendarData": {
    "syncDate": "2025-01-15T10:30:00+01:00",
    "events": [
      {
        "title": "Team standup",
        "start": "2025-01-15T09:00:00+01:00",
        "end": "2025-01-15T09:30:00+01:00",
        "location": "Room A",
        "allDay": false
      },
      {
        "title": "Lunch with Sarah",
        "start": "2025-01-15T12:00:00+01:00",
        "end": "2025-01-15T13:00:00+01:00",
        "location": "",
        "allDay": false
      },
      {
        "title": "Company holiday",
        "start": "2025-01-16T00:00:00+01:00",
        "end": "2025-01-16T23:59:59+01:00",
        "location": "",
        "allDay": true
      }
    ],
    "reminders": [
      {
        "title": "Buy groceries",
        "dueDate": "2025-01-15T18:00:00+01:00",
        "calendarItemIdentifier": "ABC-123-DEF-456",
        "list": "Shopping",
        "priority": 1,
        "completed": false
      },
      {
        "title": "Call dentist",
        "dueDate": "2025-01-16T10:00:00+01:00",
        "calendarItemIdentifier": "GHI-789-JKL-012",
        "list": "Health",
        "priority": 0,
        "completed": false
      },
      {
        "title": "Submit report",
        "dueDate": "2025-01-15T17:00:00+01:00",
        "calendarItemIdentifier": "MNO-345-PQR-678",
        "list": "Work",
        "priority": 4,
        "completed": true
      }
    ]
  }
}
```

### Field reference

| Field | Type | Max Length | Description |
|-------|------|-----------|-------------|
| `version` | int | — | Format version (`1` = flat, `2`+ = wrapped). Defaults to `1` if omitted |
| `syncDate` | string | 26 chars | ISO 8601 timestamp with timezone. `todayDate` and upcoming window are derived from this |
| `displayConfig.utcOffsetSeconds` | int | — | UTC offset in seconds (e.g. `3600` = UTC+1) |
| `displayConfig.use24HourTime` | bool | — | `true` for 24h format, `false` for 12h. Defaults to `true` |
| `events[].title` | string | 64 chars | Event name |
| `events[].start` | string | 26 chars | ISO 8601 start time |
| `events[].end` | string | 26 chars | ISO 8601 end time |
| `events[].location` | string | 64 chars | Location (optional) |
| `events[].allDay` | bool | — | All-day event flag. Defaults to `false` |
| `reminders[].title` | string | 64 chars | Reminder title |
| `reminders[].dueDate` | string | 26 chars | ISO 8601 due date |
| `reminders[].calendarItemIdentifier` | string | 48 chars | Unique ID for two-way sync |
| `reminders[].list` | string | 32 chars | List/category name (optional) |
| `reminders[].priority` | int | — | Priority level (0–4). Defaults to `0` |
| `reminders[].completed` | bool | — | Completion status. Defaults to `false` |

**Limits:** max 20 events, max 60 reminders, 24 KB BLE receive buffer.

### Read value (completions synced back to the app)

```json
{
  "completedIds": [
    "ABC-123-DEF-456",
    "MNO-345-PQR-678"
  ]
}
```

Contains the `calendarItemIdentifier` values of reminders marked complete on the device.

## Dependencies

- [open-x4-sdk](https://github.com/open-x4-epaper/community-sdk) — Hardware drivers (EInkDisplay, InputManager, BatteryMonitor, SDCardManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) v7.4.2 — JSON parsing (installed automatically by PlatformIO)


---

CrossPoint Reader is **not affiliated with Xteink or any manufacturer of the X4 hardware**.
