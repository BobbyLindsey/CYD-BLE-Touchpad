#pragma once

#include <Arduino.h>
#include <vector>
#include "BackgroundManager.h"
#include "BleHidMouse.h"
#include "CydDisplay.h"
#include "SettingsManager.h"
#include "TouchpadEngine.h"

enum class AppScreen : uint8_t {
    Touchpad,
    BootImportPrompt,
    Settings,
    SdFilePicker
};

class UiManager {
public:
    void begin(CydDisplay* lcd, SettingsManager* settings,
               BackgroundManager* bg, BleHidMouse* ble, TouchpadEngine* touchpad);

    AppScreen currentScreen() const { return _screen; }

    void applyDisplaySettings();
    void runTouchCalibration();

    void showTouchpad(bool forceBanner = true);
    void showBootImportPrompt(const String& candidatePath);
    void toggleSettings();

    // Called from main loop (~125 Hz)
    void update(bool rawTouched, int16_t touchX, int16_t touchY, GestureEvent tpEvent);

private:
    // Touchpad HUD helpers
    void drawTouchpadGuides();
    void drawStatusPill(const char* overrideText = nullptr, uint16_t accentColor = 0);
    void hideStatusPill();

    // Settings UI
    void drawSettingsScreen();
    void handleSettingsTap(int16_t x, int16_t y);

    // SD Picker & Import Progress UI
    void openSdPicker();
    void drawSdPickerScreen();
    void handleSdPickerTap(int16_t x, int16_t y);
    void executeSdImport(const String& sdPath);
    void drawProgressModal(const char* title, int percent, const char* subtitle);
    void showBlockingNotice(const char* title, const char* line1, const char* line2,
                            uint16_t accentColor, uint32_t autoCloseMs = 0);

    // Boot import prompt UI
    void drawBootPromptScreen();
    void handleBootPromptTap(int16_t x, int16_t y);

    CydDisplay*        _lcd      = nullptr;
    SettingsManager*   _settings = nullptr;
    BackgroundManager* _bg       = nullptr;
    BleHidMouse*       _ble      = nullptr;
    TouchpadEngine*    _touchpad = nullptr;

    AppScreen _screen            = AppScreen::Touchpad;
    uint8_t   _settingsPage      = 0;

    // UI touch tap edge detector (for menus)
    bool      _uiWasTouched      = false;
    uint32_t  _uiTouchDownMs     = 0;
    int16_t   _uiDownX           = 0;
    int16_t   _uiDownY           = 0;

    // Status pill tracking on Touchpad screen
    bool      _pillVisible       = false;
    uint32_t  _pillShownMs       = 0;
    bool      _lastBleConnected  = false;
    bool      _lastDragging      = false;

    // SD Picker state
    std::vector<String> _sdFiles;
    String              _sdError;
    int                 _sdPage          = 0;
    String              _bootCandidate;
};

