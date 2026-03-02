/*
 * Glance — Display module
 *
 * All e-ink rendering: fonts, drawing helpers, page views, special screens.
 *
 * Structure:
 *   Display
 *   ├── Header / Footer / Buttons
 *   ├── ReminderView (pages)
 *   │   ├── TodayPage     → EventRow
 *   │   ├── UpcomingPage   → EventRow
 *   │   └── RemindersPage  → ReminderRow
 *   └── Special screens (Welcome, Syncing, Boot, Sleep)
 */

#pragma once

void initFonts();
void updateDisplay();
void drawBootScreen();
void drawSleepScreen();
void drawSleepPageScreen();   // render a data page with sleep footer (for deep sleep display)
bool loadSleepImage();
