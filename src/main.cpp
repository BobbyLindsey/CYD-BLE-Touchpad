#include <Arduino.h>
#include "BackgroundManager.h"
#include "BleHidMouse.h"
#include "Config.h"
#include "CydDisplay.h"
#include "SettingsManager.h"
#include "TouchpadEngine.h"
#include "UiManager.h"

namespace {

CydDisplay        gLcd;
SettingsManager   gSettings;
BackgroundManager gBg;
BleHidMouse       gBle;
TouchpadEngine    gTouchpad;
UiManager         gUi;

// Boot button long-press detector (GPIO 0, active-low)
bool     gBootWasDown     = false;
bool     gBootFired       = false;
uint32_t gBootDownStartMs = 0;

// RGB LED helper (Active-Low on CYD2USB: LOW = ON, HIGH = OFF)
uint32_t gCyanFlashUntilMs = 0;

void initHardwarePins() {
    pinMode(Config::PIN_BOOT_BTN, INPUT_PULLUP);

    pinMode(Config::PIN_LED_R, OUTPUT);
    pinMode(Config::PIN_LED_G, OUTPUT);
    pinMode(Config::PIN_LED_B, OUTPUT);
    digitalWrite(Config::PIN_LED_R, HIGH);
    digitalWrite(Config::PIN_LED_G, HIGH);
    digitalWrite(Config::PIN_LED_B, HIGH);
}

void setRgbLed(bool r, bool g, bool b) {
    digitalWrite(Config::PIN_LED_R, r ? LOW : HIGH);
    digitalWrite(Config::PIN_LED_G, g ? LOW : HIGH);
    digitalWrite(Config::PIN_LED_B, b ? LOW : HIGH);
}

void updateStatusLed(uint32_t now) {
    if (!gSettings.get().statusLed) {
        setRgbLed(false, false, false);
        return;
    }

    if (now < gCyanFlashUntilMs) {
        setRgbLed(false, true, true);
        return;
    }

    if (gBle.isConnected()) {
        // Brief green confirmation for 1.2s on connect, then off to avoid glare
        if (now - gBle.lastStateChangeMs() < 1200) {
            setRgbLed(false, true, false);
        } else {
            setRgbLed(false, false, false);
        }
    } else {
        // Gentle blue pulse (100 ms every 2 seconds) while waiting to pair/reconnect
        bool pulse = ((now % 2000) < 100);
        setRgbLed(false, false, pulse);
    }
}

void pollBootButton(uint32_t now) {
    const bool down = (digitalRead(Config::PIN_BOOT_BTN) == LOW);
    if (down) {
        if (!gBootWasDown) {
            gBootWasDown     = true;
            gBootFired       = false;
            gBootDownStartMs = now;
        } else if (!gBootFired && (now - gBootDownStartMs >= Config::BOOT_LONG_PRESS_MS)) {
            gBootFired        = true;
            gCyanFlashUntilMs = now + 180;
            gUi.toggleSettings();
        }
    } else {
        gBootWasDown = false;
        gBootFired   = false;
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    initHardwarePins();

    gSettings.begin();
    gBg.begin();

    gLcd.init();
    gTouchpad.begin(&gBle, &gSettings);
    gUi.begin(&gLcd, &gSettings, &gBg, &gBle, &gTouchpad);

    // Run 4-corner calibration automatically on very first boot
    if (!gSettings.get().calibrated) {
        gUi.runTouchCalibration();
    }

    // Start BLE HID Mouse service
    gBle.begin(Config::BLE_DEVICE_NAME, Config::BLE_MANUFACTURER);
    if (gSettings.consumeBleProfileMigrated()) {
        Serial.println("[BLE] Schema v4 migration: clearing old pre-0x03C2 bond for fresh iOS Mouse profile");
        gBle.clearBonds();
    }

    // Check if an SD card with a new /bg.* image is inserted; otherwise go straight to Touchpad
    String bootSdCandidate;
    if (gBg.checkBootImportCandidate(bootSdCandidate)) {
        gUi.showBootImportPrompt(bootSdCandidate);
    } else {
        gUi.showTouchpad(true);
    }
}

void loop() {
    static uint32_t lastTickMs = 0;
    const uint32_t now = millis();

    // Run input & UI loop at ~125 Hz (every 8 ms)
    if (now - lastTickMs < 8) {
        delay(1);
        return;
    }
    lastTickMs = now;

    pollBootButton(now);
    updateStatusLed(now);

    int16_t tx = 0, ty = 0;
    const bool touched = gLcd.getTouch(&tx, &ty);

    GestureEvent evt = GestureEvent::None;
    if (gUi.currentScreen() == AppScreen::Touchpad) {
        evt = gTouchpad.update(
            touched,
            tx,
            ty,
            static_cast<int16_t>(gLcd.width()),
            static_cast<int16_t>(gLcd.height()));
    }

    gUi.update(touched, tx, ty, evt);
}

