/*
 * Glance — Display module implementation
 *
 * All e-ink rendering: fonts, format helpers, drawing primitives,
 * page views (Today / Upcoming / Reminders), and special screens.
 */

#include "display.h"
#include "globals.h"
#include "helpers.h"
#include "fontIds.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>

// Builtin fonts — NotoSans for crisp e-ink rendering
#include "builtinFonts/notosans_8_regular.h"
#include "builtinFonts/notosans_12_regular.h"
#include "builtinFonts/notosans_12_bold.h"
#include "builtinFonts/notosans_14_regular.h"
#include "builtinFonts/notosans_14_bold.h"

// ============================================================================
// FONT OBJECTS
// ============================================================================

static EpdFont smallFont(&notosans_8_regular);
static EpdFontFamily smallFontFamily(&smallFont);

// UI_10 → NotoSans 12pt (subtitles, secondary text)
static EpdFont ui10RegularFont(&notosans_12_regular);
static EpdFont ui10BoldFont(&notosans_12_bold);
static EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

// UI_12 → NotoSans 14pt (titles, primary text)
static EpdFont ui12RegularFont(&notosans_14_regular);
static EpdFont ui12BoldFont(&notosans_14_bold);
static EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

// ============================================================================
// FONT REGISTRATION
// ============================================================================

void initFonts() {
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
}

// ============================================================================
// FORMAT HELPERS
// ============================================================================

static inline bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

static void formatTime(const char* iso, char* out, size_t outSize) {
  const char* t = strchr(iso, 'T');
  if (!t || strlen(t) < 6) { out[0] = '\0'; return; }
  if (!isAsciiDigit(t[1]) || !isAsciiDigit(t[2]) || !isAsciiDigit(t[4]) || !isAsciiDigit(t[5])) {
    out[0] = '\0'; return;
  }
  int hour = (t[1] - '0') * 10 + (t[2] - '0');
  int minute = (t[4] - '0') * 10 + (t[5] - '0');

  // Apply UTC offset
  int totalMinutes = hour * 60 + minute + (utcOffsetSeconds / 60);
  while (totalMinutes < 0) totalMinutes += 1440;
  while (totalMinutes >= 1440) totalMinutes -= 1440;
  hour = totalMinutes / 60;
  minute = totalMinutes % 60;

  if (use24HourTime) {
    snprintf(out, outSize, "%d:%02d", hour, minute);
  } else {
    const char* ampm = (hour >= 12) ? "PM" : "AM";
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    snprintf(out, outSize, "%d:%02d %s", h12, minute, ampm);
  }
}

static void formatDateShort(const char* iso, char* out, size_t outSize) {
  char localDate[11];
  getLocalDate(iso, localDate);
  if (strlen(localDate) < 10) { out[0] = '\0'; return; }
  int y = (localDate[0]-'0')*1000 + (localDate[1]-'0')*100 + (localDate[2]-'0')*10 + (localDate[3]-'0');
  int m = (localDate[5]-'0')*10 + (localDate[6]-'0');
  int d = (localDate[8]-'0')*10 + (localDate[9]-'0');
  snprintf(out, outSize, "%02d.%02d.%04d", d, m, y);
}

static void formatDateLong(const char* iso, char* out, size_t outSize) {
  formatDateShort(iso, out, outSize);
}

// ============================================================================
// BATTERY DRAWING (from CrossPoint BaseTheme)
// ============================================================================

static void drawBatteryIcon(int x, int y, int battWidth, int rectHeight, uint16_t percentage) {
  // Body outline
  renderer.drawLine(x + 1, y, x + battWidth - 3, y);
  renderer.drawLine(x + 1, y + rectHeight - 1, x + battWidth - 3, y + rectHeight - 1);
  renderer.drawLine(x, y + 1, x, y + rectHeight - 2);
  renderer.drawLine(x + battWidth - 2, y + 1, x + battWidth - 2, y + rectHeight - 2);
  // Terminal nub (proportional to height)
  int nubTop = rectHeight / 4;
  int nubBot = rectHeight - nubTop - 1;
  renderer.drawPixel(x + battWidth - 1, y + nubTop);
  renderer.drawPixel(x + battWidth - 1, y + nubBot);
  renderer.drawLine(x + battWidth, y + nubTop + 1, x + battWidth, y + nubBot - 1);
  // Fill level
  int filledWidth = percentage * (battWidth - 5) / 100 + 1;
  if (filledWidth > battWidth - 5) filledWidth = battWidth - 5;
  renderer.fillRect(x + 2, y + 2, filledWidth, rectHeight - 4);
}

// ============================================================================
// DRAWING HELPERS
// ============================================================================

static void drawHeader(const char* title) {
  const int screenW = renderer.getScreenWidth();
  const uint16_t percentage = battery.readPercentage();

  // Battery icon — right-aligned, vertically centered in header
  const int batteryX = screenW - CONTENT_SIDE_PADDING - BATTERY_WIDTH;
  const int batteryY = (HEADER_HEIGHT - BATTERY_HEIGHT) / 2;
  drawBatteryIcon(batteryX, batteryY, BATTERY_WIDTH, BATTERY_HEIGHT, percentage);

  // Percentage text in UI_10 (same visual weight as title)
  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", percentage);
  const int pctWidth = renderer.getTextWidth(UI_10_FONT_ID, pctBuf);
  const int pctY = (HEADER_HEIGHT - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  renderer.drawText(UI_10_FONT_ID, batteryX - pctWidth - 6, pctY, pctBuf);

  // BLE indicator — left side
  if (bleEnabled) {
    const int bleY = (HEADER_HEIGHT - 12) / 2;
    renderer.fillRect(8, bleY, 12, 12);
    renderer.drawText(UI_10_FONT_ID, 24, pctY, bleConnected ? "BLE" : "...");
  }

  // Centered title
  if (title) {
    const int rightUsed = screenW - batteryX + pctWidth + 10;
    const int maxTitleWidth = screenW - rightUsed * 2;
    auto truncTitle = renderer.truncatedText(UI_12_FONT_ID, title, maxTitleWidth, EpdFontFamily::BOLD);
    const int titleY = (HEADER_HEIGHT - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, titleY, truncTitle.c_str(), true, EpdFontFamily::BOLD);
  }

  renderer.drawLine(0, HEADER_HEIGHT - 1, screenW - 1, HEADER_HEIGHT - 1);
}

// Draw button hints. Physical buttons L→R: BTN_BACK, BTN_CONFIRM, BTN_LEFT, BTN_RIGHT
static void drawButtonHints(const char* btn1, const char* btn2, const char* btn3, const char* btn4) {
  const int screenH = renderer.getScreenHeight();
  const int buttonY = screenH - BUTTON_HINTS_HEIGHT;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int x = BUTTON_POSITIONS[i];
      renderer.fillRect(x, buttonY, BUTTON_WIDTH, BUTTON_HINTS_HEIGHT, false);
      renderer.drawRect(x, buttonY, BUTTON_WIDTH, BUTTON_HINTS_HEIGHT);
      const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, labels[i]);
      const int textX = x + (BUTTON_WIDTH - textWidth) / 2;
      const int textY = buttonY + (BUTTON_HINTS_HEIGHT - lineH) / 2;
      renderer.drawText(UI_10_FONT_ID, textX, textY, labels[i]);
    }
  }
}

// ============================================================================
// SCROLL INDICATORS
// ============================================================================

static void drawScrollIndicators(bool canScrollUp, bool canScrollDown) {
  if (!canScrollUp && !canScrollDown) return;
  if (totalPageItems <= 0) return;

  const int screenW = renderer.getScreenWidth();
  const int trackX = screenW - 8;
  const int trackW = 4;
  const int trackTop = HEADER_HEIGHT + 4;
  const int trackBottom = renderer.getScreenHeight() - BUTTON_HINTS_HEIGHT - FOOTER_INFO_HEIGHT - 4;
  const int trackH = trackBottom - trackTop;

  // Draw track (thin outline)
  renderer.drawRect(trackX, trackTop, trackW, trackH);

  // Proportional thumb
  int thumbH = (visiblePageItems * trackH) / totalPageItems;
  if (thumbH < 8) thumbH = 8;
  if (thumbH > trackH) thumbH = trackH;

  int thumbY = trackTop;
  if (totalPageItems > visiblePageItems) {
    thumbY = trackTop + (scrollOffset * (trackH - thumbH)) / (totalPageItems - visiblePageItems);
  }

  renderer.fillRect(trackX, thumbY, trackW, thumbH);
}

static void drawPageTitle(const char* title, const char* subtitle) {
  const int top = HEADER_HEIGHT + 8;
  renderer.drawText(UI_12_FONT_ID, CONTENT_SIDE_PADDING, top, title, true, EpdFontFamily::BOLD);

  if (subtitle && subtitle[0]) {
    const int subX = CONTENT_SIDE_PADDING + renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD) + 12;
    renderer.drawText(UI_10_FONT_ID, subX, top + 4, subtitle);
  }

  const int lineY = top + renderer.getLineHeight(UI_12_FONT_ID) + 4;
  const int screenW = renderer.getScreenWidth();
  renderer.drawLine(CONTENT_SIDE_PADDING, lineY, screenW - CONTENT_SIDE_PADDING - 1, lineY);
}

// ============================================================================
// ROW RENDERERS — EventRow / ReminderRow
// ============================================================================

static int drawEventItem(int x, int y, int w, const CalendarEvent& evt, bool showDate) {
  const int rowH = EVENT_ROW_HEIGHT;

  const int textX = x;
  const int maxW = w;

  // Title (bold, 14pt)
  auto title = renderer.truncatedText(UI_12_FONT_ID, evt.title, maxW, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, textX, y, title.c_str(), true, EpdFontFamily::BOLD);

  // Subtitle: time (+ date if showDate) + location
  const int subtitleY = y + renderer.getLineHeight(UI_12_FONT_ID) + 2;
  char subtitleBuf[128];

  char timeBuf[16];
  if (evt.allDay) {
    safeCopy(timeBuf, sizeof(timeBuf), "All day");
  } else {
    formatTime(evt.start, timeBuf, sizeof(timeBuf));
  }

  if (showDate) {
    char dateBuf[16];
    formatDateShort(evt.start, dateBuf, sizeof(dateBuf));
    if (evt.location[0]) {
      snprintf(subtitleBuf, sizeof(subtitleBuf), "%s, %s - %s", dateBuf, timeBuf, evt.location);
    } else {
      snprintf(subtitleBuf, sizeof(subtitleBuf), "%s, %s", dateBuf, timeBuf);
    }
  } else {
    if (evt.location[0]) {
      snprintf(subtitleBuf, sizeof(subtitleBuf), "%s - %s", timeBuf, evt.location);
    } else {
      safeCopy(subtitleBuf, sizeof(subtitleBuf), timeBuf);
    }
  }

  auto subtitle = renderer.truncatedText(UI_10_FONT_ID, subtitleBuf, maxW);
  renderer.drawText(UI_10_FONT_ID, textX, subtitleY, subtitle.c_str());

  // Separator
  renderer.drawLine(x, y + rowH - 1, x + w - 1, y + rowH - 1);

  return rowH;
}

// Calculate reminder item height without drawing (for layout pre-check)
static int calcReminderItemHeight(const Reminder& rem, int maxTextW) {
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int cbSize = 18;
  const int pad = 10;  // 5 top + 5 bottom

  int fullWidth = renderer.getTextWidth(UI_10_FONT_ID, rem.title);
  bool twoLines = (fullWidth > maxTextW);
  bool hasDate = (rem.dueDate[0] != '\0');

  int contentH = twoLines ? (lineH * 2 - 2) : lineH;
  if (hasDate) contentH += 2 + smallLineH;

  int rowH = contentH + pad;
  if (rowH < cbSize + pad) rowH = cbSize + pad;
  return rowH;
}

static int drawReminderItem(int x, int y, int w, const Reminder& rem, bool selected) {
  const int cbSize = 18;
  const int textX = x + cbSize + 10;
  const int maxW = w - cbSize - 10;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);

  const int rowH = calcReminderItemHeight(rem, maxW);

  // Circle checkbox — vertically centered in row
  const int cbY = y + (rowH - cbSize) / 2;
  if (rem.completed) {
    renderer.fillRoundedRect(x, cbY, cbSize, cbSize, cbSize / 2, Color::Black);
  } else {
    renderer.drawRoundedRect(x, cbY, cbSize, cbSize, 2, cbSize / 2, true);
  }

  // Selection indicator: thick left bar
  if (selected) {
    renderer.fillRect(x - 8, y, 4, rowH - 1);
  }

  // Word-wrap title: up to 2 lines
  int fullWidth = renderer.getTextWidth(UI_10_FONT_ID, rem.title);
  bool twoLines = false;
  const int textY = y + 4;  // top padding

  if (fullWidth <= maxW) {
    renderer.drawText(UI_10_FONT_ID, textX, textY, rem.title);
  } else {
    int breakIdx = 0;
    char line1[64];
    int titleLen = strlen(rem.title);

    for (int i = 0; i < titleLen && i < 63; i++) {
      line1[i] = rem.title[i];
      line1[i + 1] = '\0';
      if (renderer.getTextWidth(UI_10_FONT_ID, line1) > maxW) break;
      if (rem.title[i] == ' ') breakIdx = i;
    }

    if (breakIdx == 0) {
      auto trunc = renderer.truncatedText(UI_10_FONT_ID, rem.title, maxW);
      renderer.drawText(UI_10_FONT_ID, textX, textY, trunc.c_str());
    } else {
      twoLines = true;
      char l1buf[64];
      strncpy(l1buf, rem.title, breakIdx);
      l1buf[breakIdx] = '\0';
      renderer.drawText(UI_10_FONT_ID, textX, textY, l1buf);

      const char* line2Start = rem.title + breakIdx + 1;
      while (*line2Start == ' ') line2Start++;
      auto line2 = renderer.truncatedText(UI_10_FONT_ID, line2Start, maxW);
      renderer.drawText(UI_10_FONT_ID, textX, textY + lineH - 2, line2.c_str());
    }
  }

  // Due date below text
  if (rem.dueDate[0]) {
    char dateStr[16];
    formatDateShort(rem.dueDate, dateStr, sizeof(dateStr));
    int dueDateY = twoLines ? textY + lineH * 2 - 1 : textY + lineH + 2;
    renderer.drawText(SMALL_FONT_ID, textX, dueDateY, dateStr);
  }

  // Separator line
  renderer.drawLine(x, y + rowH - 1, x + w - 1, y + rowH - 1);

  return rowH;
}

// ============================================================================
// PAGE VIEWS — ReminderView
// ============================================================================

static void drawTodayPage() {
  const int screenW = renderer.getScreenWidth();
  const int contentW = screenW - 2 * CONTENT_SIDE_PADDING;
  const int bottom = renderer.getScreenHeight() - BUTTON_HINTS_HEIGHT - FOOTER_INFO_HEIGHT;

  char dateBuf[24];
  formatDateLong(todayDate, dateBuf, sizeof(dateBuf));
  drawPageTitle(dateBuf, "");

  int y = HEADER_HEIGHT + 8 + renderer.getLineHeight(UI_12_FONT_ID) + 12;

  // Count today's events
  int todayCount = 0;
  for (int i = 0; i < eventCount; i++) {
    if (dateCompare(events[i].start, todayDate) == 0) todayCount++;
  }
  totalPageItems = todayCount;

  if (todayCount == 0) {
    const int centerY = (y + bottom) / 2 - 20;
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, "No events today");
    visiblePageItems = 0;
  } else {
    int itemIdx = 0;
    int drawn = 0;
    for (int i = 0; i < eventCount; i++) {
      if (dateCompare(events[i].start, todayDate) != 0) continue;
      if (itemIdx < scrollOffset) { itemIdx++; continue; }
      if (y + EVENT_ROW_HEIGHT > bottom) break;
      y += drawEventItem(CONTENT_SIDE_PADDING, y, contentW, events[i], false);
      drawn++;
      itemIdx++;
    }
    visiblePageItems = drawn;
  }

  drawScrollIndicators(scrollOffset > 0, scrollOffset + visiblePageItems < totalPageItems);
}

static void drawUpcomingPage() {
  const int screenW = renderer.getScreenWidth();
  const int contentW = screenW - 2 * CONTENT_SIDE_PADDING;
  const int bottom = renderer.getScreenHeight() - BUTTON_HINTS_HEIGHT - FOOTER_INFO_HEIGHT;

  drawPageTitle("Next three days", "");

  int y = HEADER_HEIGHT + 8 + renderer.getLineHeight(UI_12_FONT_ID) + 12;

  int upCount = 0;
  for (int i = 0; i < eventCount; i++) {
    if (dateCompare(events[i].start, todayDate) > 0 &&
        dateCompare(events[i].start, upcomingEndDate) <= 0)
      upCount++;
  }
  totalPageItems = upCount;

  if (upCount == 0) {
    const int centerY = (y + bottom) / 2 - 20;
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, "No upcoming events");
    visiblePageItems = 0;
  } else {
    int itemIdx = 0;
    int drawn = 0;
    for (int i = 0; i < eventCount; i++) {
      if (dateCompare(events[i].start, todayDate) <= 0) continue;
      if (dateCompare(events[i].start, upcomingEndDate) > 0) continue;
      if (itemIdx < scrollOffset) { itemIdx++; continue; }
      if (y + EVENT_ROW_HEIGHT > bottom) break;
      y += drawEventItem(CONTENT_SIDE_PADDING, y, contentW, events[i], true);
      drawn++;
      itemIdx++;
    }
    visiblePageItems = drawn;
  }

  drawScrollIndicators(scrollOffset > 0, scrollOffset + visiblePageItems < totalPageItems);
}

static void drawRemindersPage() {
  const int screenW = renderer.getScreenWidth();
  const int contentW = screenW - 2 * CONTENT_SIDE_PADDING;
  const int bottom = renderer.getScreenHeight() - BUTTON_HINTS_HEIGHT - FOOTER_INFO_HEIGHT;

  int y = HEADER_HEIGHT + 8;
  totalPageItems = reminderCount;

  if (reminderCount == 0) {
    const int centerY = (y + bottom) / 2 - 20;
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, "No reminders");
    visiblePageItems = 0;
  } else {
    int drawn = 0;
    const char* currentList = "";
    const int subHeaderH = renderer.getLineHeight(UI_12_FONT_ID) + 8;
    const int maxTextW = contentW - 18 - 10;  // cbSize + gap

    for (int i = scrollOffset; i < reminderCount; i++) {
      int itemH = calcReminderItemHeight(reminders[i], maxTextW);

      // Draw list subheader when list name changes
      if (reminders[i].list[0] && strcmp(reminders[i].list, currentList) != 0) {
        currentList = reminders[i].list;
        if (y + subHeaderH + itemH > bottom) break;
        renderer.drawText(UI_12_FONT_ID, CONTENT_SIDE_PADDING, y + 2, currentList, true, EpdFontFamily::BOLD);
        y += subHeaderH;
      }

      if (y + itemH > bottom) break;
      bool isSelected = (i == selectedReminderIdx);
      y += drawReminderItem(CONTENT_SIDE_PADDING, y, contentW, reminders[i], isSelected);
      drawn++;
    }
    visiblePageItems = drawn;
  }

  if (Serial) Serial.printf("[DISP] Reminders: total=%d visible=%d scroll=%d sel=%d\n",
                             totalPageItems, visiblePageItems, scrollOffset, selectedReminderIdx);
  drawScrollIndicators(scrollOffset > 0, scrollOffset + visiblePageItems < totalPageItems);
}

// ============================================================================
// SPECIAL SCREENS
// ============================================================================

static void drawWelcomeScreen() {
  drawHeader("Glance");

  const int centerY = renderer.getScreenHeight() / 2 - 60;
  renderer.drawCenteredText(UI_12_FONT_ID, centerY, "Welcome to Glance", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, centerY + 45, "Press \"Sync\" to connect your iPhone");
  renderer.drawCenteredText(UI_10_FONT_ID, centerY + 75, "and send your calendar data.");
  renderer.drawCenteredText(SMALL_FONT_ID, centerY + 120, FIRMWARE_VERSION);

  drawButtonHints("", "", "", "Sync");
}

static void drawSyncingScreen() {
  drawHeader("Syncing...");

  const int centerY = renderer.getScreenHeight() / 2 - 40;
  if (bleConnected) {
    renderer.drawCenteredText(UI_12_FONT_ID, centerY, "Receiving data...", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 40, "Please wait while data transfers.");
  } else {
    renderer.drawCenteredText(UI_12_FONT_ID, centerY, "Waiting for connection...", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 40, "Open the iOS app and tap Sync");
  }

  drawButtonHints("", "", "", "Cancel");
}

void drawBootScreen() {
  const int centerY = renderer.getScreenHeight() / 2 - 30;
  renderer.drawCenteredText(UI_12_FONT_ID, centerY, "Glance", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, centerY + 40, "Starting up...");
  renderer.drawCenteredText(SMALL_FONT_ID, centerY + 75, FIRMWARE_VERSION);
}

void drawSleepScreen() {
  const int centerY = renderer.getScreenHeight() / 2 - 20;
  renderer.drawCenteredText(UI_12_FONT_ID, centerY, "Sleeping...", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, centerY + 40, "Press power to wake");
}

// ============================================================================
// SLEEP IMAGE LOADING
// ============================================================================

// Load a BMP from SD card into the framebuffer.
// Supports 1-bit and 8-bit BMPs in landscape (800x480) or portrait (480x800).
// 8-bit images are dithered to 1-bit using Floyd-Steinberg error diffusion.
// Portrait images are rotated to the physical (landscape) framebuffer layout.
static bool loadSleepBMP() {
  if (!SDCard.ready() || !SDCard.exists("/sleep.bmp")) return false;

  FsFile file;
  if (!SDCard.openFileForRead("SLP", "/sleep.bmp", file)) return false;

  // BMP file header (14 bytes) + read DIB header size to validate format
  uint8_t bmpHdr[18];
  if (file.read(bmpHdr, 18) != 18) { file.close(); return false; }

  if (bmpHdr[0] != 'B' || bmpHdr[1] != 'M') {
    if (Serial) Serial.println("[SLP] Not a BMP file");
    file.close(); return false;
  }

  uint32_t dataOffset = bmpHdr[10] | (bmpHdr[11] << 8) | (bmpHdr[12] << 16) | (bmpHdr[13] << 24);

  uint32_t dibSize = bmpHdr[14] | (bmpHdr[15] << 8) | (bmpHdr[16] << 16) | (bmpHdr[17] << 24);
  if (dibSize != 40) {
    if (Serial) Serial.printf("[SLP] Unsupported DIB header size: %d (expected 40)\n", (int)dibSize);
    file.close(); return false;
  }

  // Read remaining 36 bytes of DIB header
  uint8_t hdr[36];
  if (file.read(hdr, 36) != 36) { file.close(); return false; }

  int32_t  width  = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | (hdr[3] << 24);
  int32_t  height = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | (hdr[7] << 24);
  uint16_t bpp    = hdr[10] | (hdr[11] << 8);

  bool topDown = (height < 0);
  if (topDown) height = -height;

  const bool isLandscape = (width == 800 && height == 480);
  const bool isPortrait  = (width == 480 && height == 800);

  if ((!isLandscape && !isPortrait) || (bpp != 1 && bpp != 8 && bpp != 24)) {
    if (Serial) Serial.printf("[SLP] BMP must be 800x480 or 480x800, 1/8/24-bit (got %dx%d %d-bit)\n",
                               (int)width, (int)height, bpp);
    file.close(); return false;
  }

  uint8_t* fb = halDisplay.getFrameBuffer();

  if (bpp == 1) {
    // --- 1-bit BMP ---
    // Read 2-entry color table (8 bytes) to detect inversion
    uint8_t colorTable[8];
    if (file.read(colorTable, 8) != 8) { file.close(); return false; }
    bool invert = (colorTable[0] >= 0x80);

    file.seekSet(dataOffset);
    const int rowBytes = width / 8;

    if (isLandscape) {
      // Direct copy to framebuffer (original fast path)
      for (int row = 0; row < 480; row++) {
        int destRow = topDown ? row : (479 - row);
        if (file.read(fb + destRow * 100, rowBytes) != rowBytes) {
          if (Serial) Serial.printf("[SLP] BMP read error at row %d\n", row);
          file.close(); return false;
        }
      }
    } else {
      // Portrait 1-bit: read row by row and rotate to physical layout
      // Portrait (imgCol, imgRow) → physical (phyX=imgRow, phyY=479-imgCol)
      uint8_t rowBuf[60];  // 480 / 8
      for (int fileRow = 0; fileRow < 800; fileRow++) {
        if (file.read(rowBuf, rowBytes) != rowBytes) {
          if (Serial) Serial.printf("[SLP] BMP read error at row %d\n", fileRow);
          file.close(); return false;
        }
        const int imgRow = topDown ? fileRow : (799 - fileRow);
        for (int imgCol = 0; imgCol < 480; imgCol++) {
          const bool bit = (rowBuf[imgCol / 8] >> (7 - (imgCol % 8))) & 1;
          const int phyX = imgRow;
          const int phyY = 479 - imgCol;
          const int destByte = phyY * 100 + phyX / 8;
          const int destBit  = 7 - (phyX % 8);
          if (bit) fb[destByte] |=  (1 << destBit);
          else     fb[destByte] &= ~(1 << destBit);
        }
      }
    }

    if (invert) {
      for (uint32_t i = 0; i < HalDisplay::BUFFER_SIZE; i++) fb[i] = ~fb[i];
    }

  } else {
    // --- 8-bit or 24-bit BMP with Floyd-Steinberg dithering ---
    uint8_t grayLUT[256] = {};

    if (bpp == 8) {
      // Read 256-entry color table and build grayscale LUT
      uint8_t colorTable[1024];
      if (file.read(colorTable, 1024) != 1024) {
        if (Serial) Serial.println("[SLP] Failed to read 8-bit color table");
        file.close(); return false;
      }
      for (int i = 0; i < 256; i++) {
        grayLUT[i] = (colorTable[i * 4 + 2] * 77 +    // R
                       colorTable[i * 4 + 1] * 150 +   // G
                       colorTable[i * 4 + 0] * 29) >> 8; // B
      }
    }

    file.seekSet(dataOffset);
    const int imgWidth  = width;
    const int imgHeight = height;
    const int rowStride = ((imgWidth * (bpp / 8)) + 3) & ~3;  // 4-byte aligned

    // Error diffusion buffers (+2 for boundary padding on each side)
    int16_t* errCur  = (int16_t*)calloc(imgWidth + 2, sizeof(int16_t));
    int16_t* errNext = (int16_t*)calloc(imgWidth + 2, sizeof(int16_t));
    uint8_t* rowBuf  = (uint8_t*)malloc(rowStride);

    if (!errCur || !errNext || !rowBuf) {
      free(errCur); free(errNext); free(rowBuf);
      if (Serial) Serial.println("[SLP] Out of memory for dithering");
      file.close(); return false;
    }

    for (int fileRow = 0; fileRow < imgHeight; fileRow++) {
      if ((int)file.read(rowBuf, rowStride) != rowStride) {
        if (Serial) Serial.printf("[SLP] BMP read error at row %d\n", fileRow);
        free(errCur); free(errNext); free(rowBuf);
        file.close(); return false;
      }

      const int imgRow = topDown ? fileRow : (imgHeight - 1 - fileRow);
      memset(errNext, 0, (imgWidth + 2) * sizeof(int16_t));

      for (int col = 0; col < imgWidth; col++) {
        uint8_t pixelGray;
        if (bpp == 8) {
          pixelGray = grayLUT[rowBuf[col]];
        } else {
          const int off = col * 3;
          pixelGray = (rowBuf[off + 2] * 77 + rowBuf[off + 1] * 150 + rowBuf[off] * 29) >> 8;
        }

        int16_t gray = pixelGray + errCur[col + 1];
        if (gray < 0) gray = 0;
        if (gray > 255) gray = 255;

        const bool isWhite = (gray >= 128);
        const int16_t err = gray - (isWhite ? 255 : 0);

        // Map image coordinates to physical framebuffer
        int phyX, phyY;
        if (isLandscape) {
          phyX = col;
          phyY = imgRow;
        } else {
          phyX = imgRow;
          phyY = 479 - col;
        }

        const int destByte = phyY * 100 + phyX / 8;
        const int destBit  = 7 - (phyX % 8);
        if (isWhite) fb[destByte] |=  (1 << destBit);
        else         fb[destByte] &= ~(1 << destBit);

        // Floyd-Steinberg error distribution
        errCur[col + 2]  += (err * 7) >> 4;  // right
        errNext[col]     += (err * 3) >> 4;  // below-left
        errNext[col + 1] += (err * 5) >> 4;  // below
        errNext[col + 2] += (err * 1) >> 4;  // below-right
      }

      // Swap error buffers
      int16_t* tmp = errCur;
      errCur = errNext;
      errNext = tmp;
    }

    free(errCur);
    free(errNext);
    free(rowBuf);
  }

  file.close();
  if (Serial) Serial.printf("[SLP] Loaded sleep.bmp (%dx%d %d-bit%s)\n",
                             (int)width, (int)height, bpp, isPortrait ? " portrait" : "");
  return true;
}

// Fallback: load raw framebuffer dump (exactly 48000 bytes, 800x480, 1-bit, row-major).
static bool loadSleepRaw() {
  if (!SDCard.ready() || !SDCard.exists("/sleep.raw")) return false;

  FsFile file;
  if (!SDCard.openFileForRead("SLP", "/sleep.raw", file)) return false;

  uint8_t* fb = halDisplay.getFrameBuffer();
  size_t bytesRead = file.read(fb, HalDisplay::BUFFER_SIZE);
  file.close();

  if (bytesRead != HalDisplay::BUFFER_SIZE) {
    if (Serial) Serial.printf("[SLP] sleep.raw wrong size: %d (expected %d)\n",
                               (int)bytesRead, (int)HalDisplay::BUFFER_SIZE);
    return false;
  }

  if (Serial) Serial.println("[SLP] Loaded sleep.raw");
  return true;
}

// Try BMP first, then raw, then return false for text fallback.
bool loadSleepImage() {
  return loadSleepBMP() || loadSleepRaw();
}

// ============================================================================
// DISPLAY UPDATE
// ============================================================================

void updateDisplay() {
  if (!needsDisplayUpdate) return;

  if (Serial) Serial.println("[DISP] Updating display...");

  renderer.clearScreen(0xFF);

  if (bleEnabled) {
    drawSyncingScreen();
  } else if (!hasData) {
    drawWelcomeScreen();
  } else {
    drawHeader(MODE_TITLES[currentMode]);

    switch (currentMode) {
      case MODE_TODAY:     drawTodayPage();     break;
      case MODE_UPCOMING:  drawUpcomingPage();  break;
      case MODE_REMINDERS: drawRemindersPage(); break;
      default:             drawTodayPage();     break;
    }

    // Footer info row — between content and button hints
    {
      const int footerY = renderer.getScreenHeight() - BUTTON_HINTS_HEIGHT - FOOTER_INFO_HEIGHT - 1;
      const int screenW = renderer.getScreenWidth();

      // Page indicator (left)
      char pageBuf[8];
      snprintf(pageBuf, sizeof(pageBuf), "%d / %d", currentMode + 1, MODE_COUNT);
      renderer.drawText(SMALL_FONT_ID, CONTENT_SIDE_PADDING, footerY, pageBuf);

      // Sync time (right)
      if (lastSyncTime[0]) {
        char dateStr[16], timeStr[16];
        formatDateShort(lastSyncTime, dateStr, sizeof(dateStr));
        formatTime(lastSyncTime, timeStr, sizeof(timeStr));
        char syncBuf[48];
        snprintf(syncBuf, sizeof(syncBuf), "Synced: %s %s", dateStr, timeStr);
        const int tw = renderer.getTextWidth(SMALL_FONT_ID, syncBuf);
        renderer.drawText(SMALL_FONT_ID, screenW - tw - CONTENT_SIDE_PADDING, footerY, syncBuf);
      }
    }

    // Buttons: ← → Toggle(on reminders) Sync
    if (currentMode == MODE_REMINDERS && reminderCount > 0) {
      drawButtonHints("<", ">", "Toggle", "Sync");
    } else {
      drawButtonHints("<", ">", "", "Sync");
    }
  }

  const bool fast = useFastRefresh;
  renderer.displayBuffer(fast ? HalDisplay::FAST_REFRESH : HalDisplay::HALF_REFRESH);
  needsDisplayUpdate = false;
  useFastRefresh = false;
  lastActivityTime = millis();

  if (Serial) Serial.printf("[DISP] Display updated (%s)\n", fast ? "fast" : "half");
}
