#pragma once

#include <Arduino.h>

namespace Config {

// ---------------------------------------------------------------------------
// CYD2USB (ESP32-2432S028 USB-C + Micro-USB, ST7789) Hardware Pin Mapping
// ---------------------------------------------------------------------------

// Display (ST7789 on Hardware HSPI)
constexpr int PIN_TFT_MOSI = 13;
constexpr int PIN_TFT_MISO = 12;
constexpr int PIN_TFT_SCLK = 14;
constexpr int PIN_TFT_CS   = 15;
constexpr int PIN_TFT_DC   = 2;
constexpr int PIN_TFT_RST  = -1;
constexpr int PIN_TFT_BL   = 21;

// Resistive Touch (XPT2046 on LovyanGFX Software SPI)
constexpr int PIN_TOUCH_MOSI = 32;
constexpr int PIN_TOUCH_MISO = 39;
constexpr int PIN_TOUCH_SCLK = 25;
constexpr int PIN_TOUCH_CS   = 33;
constexpr int PIN_TOUCH_IRQ  = 36;

// MicroSD Card Slot (Hardware VSPI)
constexpr int PIN_SD_MOSI = 23;
constexpr int PIN_SD_MISO = 19;
constexpr int PIN_SD_SCLK = 18;
constexpr int PIN_SD_CS   = 5;
constexpr uint32_t SD_SPI_FREQ_HZ = 20000000;

// Onboard RGB LED (Active-Low: LOW = ON, HIGH = OFF)
constexpr int PIN_LED_R = 4;
constexpr int PIN_LED_G = 16;
constexpr int PIN_LED_B = 17;

// Hardware BOOT Button (Active-Low)
constexpr int PIN_BOOT_BTN = 0;
constexpr uint32_t BOOT_LONG_PRESS_MS = 1500;

// ---------------------------------------------------------------------------
// BLE HID Configuration
// ---------------------------------------------------------------------------
constexpr const char* BLE_DEVICE_NAME  = "CYD Mouse";
constexpr const char* BLE_MANUFACTURER = "CYD Labs";

// ---------------------------------------------------------------------------
// Flash Background Storage Paths (LittleFS)
// ---------------------------------------------------------------------------
constexpr const char* BG_PATH_LANDSCAPE = "/bg_land.bin"; // 320 x 240 RGB565 (153,600 bytes)
constexpr const char* BG_PATH_PORTRAIT  = "/bg_port.bin"; // 240 x 320 RGB565 (153,600 bytes)
constexpr const char* BG_PATH_TEMP      = "/bg_tmp.bin";
constexpr const char* BG_META_PATH      = "/bg_meta.txt"; // Stores last imported filename/size

// ---------------------------------------------------------------------------
// Touchpad Geometry & Gesture Timing
// ---------------------------------------------------------------------------
constexpr int VSCROLL_STRIP_WIDTH   = 36;   // Vertical scroll strip width in px (~6.4mm)
constexpr int HSCROLL_STRIP_HEIGHT  = 34;   // Horizontal scroll strip height in px (~6.0mm)
constexpr int CORNER_RCLICK_W       = 40;   // Optional bottom corner right-click target
constexpr int CORNER_RCLICK_H       = 36;

constexpr uint32_t TAP_MIN_MS                  = 16;   // Min contact duration (with SETTLE_SAMPLES=2 -> >=24ms)
constexpr uint32_t TAP_MAX_MS                  = 500;  // Max contact duration to register as a resistive tap
constexpr uint32_t PENDING_TAP_MS              = 400;  // Air-gap window after tap release for touch-release-touch-drag
constexpr uint32_t POST_MOTION_TAP_SUPPRESS_MS = 45;   // Brief guard against resistive lift-off bounce (<45ms)
constexpr uint32_t LONG_PRESS_MS               = 1000; // Default stationary hold / Long Click duration (1.0s)
constexpr int      TAP_SLOP_PX                 = 10;   // Max movement for corner tap
constexpr float    MOVE_START_PX               = 10.5f;// Smoothed displacement (~1.85mm) to start cursor motion
constexpr float    EMA_ALPHA                   = 0.55f;
constexpr int      SETTLE_SAMPLES              = 2;    // Initial resistive make-contact frames discarded

// Status HUD auto-hide timeout
constexpr uint32_t HUD_AUTOHIDE_MS     = 4000;

} // namespace Config

