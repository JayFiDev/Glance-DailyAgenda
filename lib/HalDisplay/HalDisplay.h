#pragma once
#include <Arduino.h>
#include <EInkDisplay.h>

class HalDisplay {
 public:
  explicit HalDisplay(EInkDisplay& eink) : einkDisplay(eink) {}

  enum RefreshMode {
    FULL_REFRESH,
    HALF_REFRESH,
    FAST_REFRESH
  };

  void begin() { einkDisplay.begin(); }

  static constexpr uint16_t DISPLAY_WIDTH = EInkDisplay::DISPLAY_WIDTH;
  static constexpr uint16_t DISPLAY_HEIGHT = EInkDisplay::DISPLAY_HEIGHT;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  void clearScreen(uint8_t color = 0xFF) const { einkDisplay.clearScreen(color); }

  void displayBuffer(RefreshMode mode = FAST_REFRESH, bool turnOff = false) {
    EInkDisplay::RefreshMode einkMode;
    switch (mode) {
      case FULL_REFRESH: einkMode = EInkDisplay::FULL_REFRESH; break;
      case HALF_REFRESH: einkMode = EInkDisplay::HALF_REFRESH; break;
      default:           einkMode = EInkDisplay::FAST_REFRESH; break;
    }
    einkDisplay.displayBuffer(einkMode, turnOff);
  }

  void deepSleep() { einkDisplay.deepSleep(); }
  uint8_t* getFrameBuffer() const { return einkDisplay.getFrameBuffer(); }

 private:
  EInkDisplay& einkDisplay;
};
