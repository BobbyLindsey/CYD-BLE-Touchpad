#pragma once

#include <Arduino.h>
#include <Preferences.h>

enum class ScrollMode : uint8_t {
    RightVertical      = 0,
    LeftVertical       = 1,
    RightAndBottomBoth = 2,
    Disabled           = 3
};

enum class OverlayMode : uint8_t {
    AutoHide = 0,
    Always   = 1,
    Hidden   = 2
};

enum class HomeGestureMode : uint8_t {
    Both       = 0, // Bottom-right corner button + Bottom-edge swipe up
    CornerOnly = 1, // Bottom-right corner button only
    SwipeOnly  = 2, // Bottom-edge swipe up only
    Disabled   = 3
};

enum class ShortcutAction : uint8_t {
    None        = 0,
    Home        = 1,  // Consumer Menu (0x0040 - Apple iOS Home Button)
    AppSwitcher = 2,  // Double Consumer Menu (0x0040 x2 - iOS App Switcher)
    Spotlight   = 3,  // Consumer AC Search (0x0221)
    Siri        = 4,  // Consumer Voice Command (0x00CF)
    LockScreen  = 5,  // Consumer Power (0x0030)
    PlayPause   = 6,  // Consumer Play/Pause (0x00CD)
    RightClick  = 7,  // Mouse Button 2
    MiddleClick = 8,  // Mouse Button 3
    Button4     = 9,  // Mouse Button 4 (customizable in iOS AssistiveTouch)
    Button5     = 10, // Mouse Button 5 (customizable in iOS AssistiveTouch)
    OpenMenu    = 11, // Open CYD Settings Menu
    COUNT       = 12
};

constexpr uint8_t LONG_CLICK_OPTIONS_COUNT = 5;

struct TouchpadSettings {
    uint8_t         sensitivity      = 1;     // 0=Low, 1=Med, 2=High, 3=Max
    uint8_t         rotation         = 0;     // 0=Portrait (240x320), 1=Landscape (320x240), 2=PortFlip, 3=LandFlip
    ScrollMode      scrollMode       = ScrollMode::RightAndBottomBoth;
    bool            naturalScroll    = false; // Invert wheel/pan direction
    bool            cornerRightClick = false; // Legacy bottom-left corner right-click
    HomeGestureMode homeGestureMode  = HomeGestureMode::Both;
    ShortcutAction  singleTapAction  = ShortcutAction::Home;
    ShortcutAction  doubleTapAction  = ShortcutAction::AppSwitcher;
    ShortcutAction  tripleTapAction  = ShortcutAction::Spotlight;
    ShortcutAction  longPressAction  = ShortcutAction::LockScreen;
    uint8_t         longClickIdx     = 2;     // 0=0.5s, 1=0.75s, 2=1.0s (default), 3=1.5s, 4=2.0s
    OverlayMode     overlayMode      = OverlayMode::AutoHide;
    uint8_t         brightnessIdx    = 3;     // 0=25%, 1=50%, 2=75%, 3=100%
    bool            invertColors     = false; // ST7789 batch display inversion toggle
    bool            statusLed        = true;  // Enable RGB LED BLE status indicator

    bool            calibrated       = false;
    uint8_t         calRotation      = 0;
    uint16_t        calData[8]       = {0};
};

class SettingsManager {
public:
    void begin();
    void save();
    void saveCalibration(const uint16_t data[8], uint8_t rotation);
    void resetDefaults();
    bool consumeBleProfileMigrated();

    TouchpadSettings& get() { return _settings; }
    const TouchpadSettings& get() const { return _settings; }

    float getBaseGain() const;
    float getAccelFactor() const;
    uint32_t getLongClickMs() const;
    uint8_t getBrightnessPwm() const;
    bool isLandscape() const { return (_settings.rotation % 2) == 1; }

    static const char* sensitivityLabel(uint8_t idx);
    static const char* rotationLabel(uint8_t rot);
    static const char* scrollModeLabel(ScrollMode mode);
    static const char* homeGestureModeLabel(HomeGestureMode mode);
    static const char* shortcutActionLabel(ShortcutAction action);
    static const char* longClickLabel(uint8_t idx);
    static const char* overlayModeLabel(OverlayMode mode);
    static const char* brightnessLabel(uint8_t idx);

private:
    TouchpadSettings _settings;
    Preferences      _prefs;
    bool             _bleProfileMigrated = false;
};

