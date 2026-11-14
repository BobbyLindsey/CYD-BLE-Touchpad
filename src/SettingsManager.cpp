#include "SettingsManager.h"

namespace {
constexpr const char* PREFS_NS            = "cyd_tp";
constexpr uint32_t    SETTINGS_MAGIC      = 0x43594432; // "CYD2"
constexpr uint8_t     SETTINGS_SCHEMA_VER = 6;          // v6: Configurable Long Click duration (1.0s default)
} // namespace

void SettingsManager::begin() {
    _prefs.begin(PREFS_NS, false);
    uint32_t magic = _prefs.getUInt("magic", 0);
    if (magic != SETTINGS_MAGIC) {
        _settings = TouchpadSettings{};
        save();
        return;
    }

    const uint8_t schemaVer    = _prefs.getUChar("ver", 1);
    _settings.sensitivity      = _prefs.getUChar("sens", 1);
    _settings.rotation         = _prefs.getUChar("rot", 0) & 0x03;
    _settings.scrollMode       = static_cast<ScrollMode>(
        _prefs.getUChar("scrmode", static_cast<uint8_t>(ScrollMode::RightAndBottomBoth)) % 4);
    _settings.naturalScroll    = _prefs.getBool("natscr", false);
    _settings.cornerRightClick = _prefs.getBool("crnrrc", false);
    _settings.homeGestureMode  = static_cast<HomeGestureMode>(
        _prefs.getUChar("homemode", static_cast<uint8_t>(HomeGestureMode::Both)) % 4);
    const uint8_t actCount = static_cast<uint8_t>(ShortcutAction::COUNT);
    _settings.singleTapAction  = static_cast<ShortcutAction>(
        _prefs.getUChar("act_1x", static_cast<uint8_t>(ShortcutAction::Home)) % actCount);
    _settings.doubleTapAction  = static_cast<ShortcutAction>(
        _prefs.getUChar("act_2x", static_cast<uint8_t>(ShortcutAction::AppSwitcher)) % actCount);
    _settings.tripleTapAction  = static_cast<ShortcutAction>(
        _prefs.getUChar("act_3x", static_cast<uint8_t>(ShortcutAction::Spotlight)) % actCount);
    _settings.longPressAction  = static_cast<ShortcutAction>(
        _prefs.getUChar("act_long", static_cast<uint8_t>(ShortcutAction::LockScreen)) % actCount);
    _settings.longClickIdx     = _prefs.getUChar("lclk_idx", 2) % LONG_CLICK_OPTIONS_COUNT;
    _settings.overlayMode      = static_cast<OverlayMode>(_prefs.getUChar("ovlmode", 0) % 3);
    _settings.brightnessIdx    = _prefs.getUChar("bright", 3) & 0x03;
    _settings.invertColors     = _prefs.getBool("invert", false);
    _settings.statusLed        = _prefs.getBool("led", true);
    _settings.calibrated       = _prefs.getBool("cal_ok", false);
    _settings.calRotation      = _prefs.getUChar("cal_rot", 0) & 0x03;

    if (_settings.calibrated) {
        size_t len = _prefs.getBytes("cal_dat", _settings.calData, sizeof(_settings.calData));
        if (len != sizeof(_settings.calData)) {
            _settings.calibrated = false;
        }
    }

    if (schemaVer < SETTINGS_SCHEMA_VER) {
        if (schemaVer < 2) {
            _settings.rotation = 0; // v2: Default to Portrait while preserving calibration
        }
        if (schemaVer < 3) {
            _settings.scrollMode = ScrollMode::RightAndBottomBoth; // v3: Enable vertical + horizontal scroll bars
        }
        if (schemaVer < 4) {
            _bleProfileMigrated = true; // v4: Clear stale pre-0x03C2 BLE bond once while keeping calibration
        }
        if (schemaVer < 5) {
            _bleProfileMigrated = true; // v5: Refresh BLE HID Report Map for Consumer Control Report ID 2
        }
        if (schemaVer < 6) {
            _settings.longClickIdx = 2; // v6: Default Long Click duration = 1.0s (no BLE re-pair needed)
        }
        save();
    }
}

bool SettingsManager::consumeBleProfileMigrated() {
    bool migrated = _bleProfileMigrated;
    _bleProfileMigrated = false;
    return migrated;
}

void SettingsManager::save() {
    _prefs.putUInt("magic", SETTINGS_MAGIC);
    _prefs.putUChar("ver", SETTINGS_SCHEMA_VER);
    _prefs.putUChar("sens", _settings.sensitivity);
    _prefs.putUChar("rot", _settings.rotation);
    _prefs.putUChar("scrmode", static_cast<uint8_t>(_settings.scrollMode));
    _prefs.putBool("natscr", _settings.naturalScroll);
    _prefs.putBool("crnrrc", _settings.cornerRightClick);
    _prefs.putUChar("homemode", static_cast<uint8_t>(_settings.homeGestureMode));
    _prefs.putUChar("act_1x", static_cast<uint8_t>(_settings.singleTapAction));
    _prefs.putUChar("act_2x", static_cast<uint8_t>(_settings.doubleTapAction));
    _prefs.putUChar("act_3x", static_cast<uint8_t>(_settings.tripleTapAction));
    _prefs.putUChar("act_long", static_cast<uint8_t>(_settings.longPressAction));
    _prefs.putUChar("lclk_idx", _settings.longClickIdx % LONG_CLICK_OPTIONS_COUNT);
    _prefs.putUChar("ovlmode", static_cast<uint8_t>(_settings.overlayMode));
    _prefs.putUChar("bright", _settings.brightnessIdx);
    _prefs.putBool("invert", _settings.invertColors);
    _prefs.putBool("led", _settings.statusLed);
    _prefs.putBool("cal_ok", _settings.calibrated);
    _prefs.putUChar("cal_rot", _settings.calRotation);
}

void SettingsManager::saveCalibration(const uint16_t data[8], uint8_t rotation) {
    memcpy(_settings.calData, data, sizeof(_settings.calData));
    _settings.calibrated  = true;
    _settings.calRotation = rotation & 0x03;
    _prefs.putBool("cal_ok", true);
    _prefs.putUChar("cal_rot", _settings.calRotation);
    _prefs.putBytes("cal_dat", _settings.calData, sizeof(_settings.calData));
}

void SettingsManager::resetDefaults() {
    bool wasCalibrated = _settings.calibrated;
    uint8_t prevCalRot = _settings.calRotation;
    uint16_t prevCal[8];
    memcpy(prevCal, _settings.calData, sizeof(prevCal));

    _settings = TouchpadSettings{};
    _settings.calibrated  = wasCalibrated;
    _settings.calRotation = prevCalRot;
    memcpy(_settings.calData, prevCal, sizeof(prevCal));
    save();
}

float SettingsManager::getBaseGain() const {
    switch (_settings.sensitivity) {
        case 0:  return 0.80f;
        case 1:  return 1.25f;
        case 2:  return 1.80f;
        default: return 2.50f;
    }
}

float SettingsManager::getAccelFactor() const {
    switch (_settings.sensitivity) {
        case 0:  return 0.04f;
        case 1:  return 0.07f;
        case 2:  return 0.10f;
        default: return 0.14f;
    }
}

uint32_t SettingsManager::getLongClickMs() const {
    switch (_settings.longClickIdx % LONG_CLICK_OPTIONS_COUNT) {
        case 0:  return 500;
        case 1:  return 750;
        case 2:  return 1000;
        case 3:  return 1500;
        default: return 2000;
    }
}

uint8_t SettingsManager::getBrightnessPwm() const {
    switch (_settings.brightnessIdx) {
        case 0:  return 64;  // 25%
        case 1:  return 128; // 50%
        case 2:  return 192; // 75%
        default: return 255; // 100%
    }
}

const char* SettingsManager::sensitivityLabel(uint8_t idx) {
    switch (idx) {
        case 0:  return "Low";
        case 1:  return "Medium";
        case 2:  return "High";
        default: return "Max";
    }
}

const char* SettingsManager::rotationLabel(uint8_t rot) {
    switch (rot & 0x03) {
        case 0:  return "Portrait";
        case 1:  return "Landscape";
        case 2:  return "Portrait Flip";
        default: return "Landscape Flip";
    }
}

const char* SettingsManager::scrollModeLabel(ScrollMode mode) {
    switch (mode) {
        case ScrollMode::RightVertical:      return "Right Edge";
        case ScrollMode::LeftVertical:       return "Left Edge";
        case ScrollMode::RightAndBottomBoth: return "Right + Bottom";
        default:                             return "Disabled";
    }
}

const char* SettingsManager::homeGestureModeLabel(HomeGestureMode mode) {
    switch (mode) {
        case HomeGestureMode::Both:       return "Both";
        case HomeGestureMode::CornerOnly: return "Corner Btn";
        case HomeGestureMode::SwipeOnly:  return "Swipe Up";
        default:                          return "Off";
    }
}

const char* SettingsManager::shortcutActionLabel(ShortcutAction action) {
    switch (action) {
        case ShortcutAction::Home:        return "Home";
        case ShortcutAction::AppSwitcher: return "App Switch";
        case ShortcutAction::Spotlight:   return "Spotlight";
        case ShortcutAction::Siri:        return "Siri";
        case ShortcutAction::LockScreen:  return "Lock Screen";
        case ShortcutAction::PlayPause:   return "Play/Pause";
        case ShortcutAction::RightClick:  return "Right Click";
        case ShortcutAction::MiddleClick: return "Mid Click";
        case ShortcutAction::Button4:     return "Button 4";
        case ShortcutAction::Button5:     return "Button 5";
        case ShortcutAction::OpenMenu:    return "CYD Menu";
        default:                          return "None";
    }
}

const char* SettingsManager::longClickLabel(uint8_t idx) {
    switch (idx % LONG_CLICK_OPTIONS_COUNT) {
        case 0:  return "0.5s";
        case 1:  return "0.75s";
        case 2:  return "1.0s";
        case 3:  return "1.5s";
        default: return "2.0s";
    }
}

const char* SettingsManager::overlayModeLabel(OverlayMode mode) {
    switch (mode) {
        case OverlayMode::AutoHide: return "Auto-Hide";
        case OverlayMode::Always:   return "Always On";
        default:                    return "Hidden";
    }
}

const char* SettingsManager::brightnessLabel(uint8_t idx) {
    switch (idx & 0x03) {
        case 0:  return "25%";
        case 1:  return "50%";
        case 2:  return "75%";
        default: return "100%";
    }
}

