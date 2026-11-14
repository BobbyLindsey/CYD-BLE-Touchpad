#include "UiManager.h"
#include "Config.h"

namespace {

constexpr uint16_t COL_BG_DARK     = 0x1082; // Deep slate
constexpr uint16_t COL_CARD        = 0x1926; // Card fill
constexpr uint16_t COL_CARD_BORDER = 0x39E7; // Subtle border
constexpr uint16_t COL_ACCENT      = 0x05BF; // Cyan-blue accent
constexpr uint16_t COL_GREEN       = 0x2689; // Connected green
constexpr uint16_t COL_AMBER       = 0xFD60; // Pairing amber
constexpr uint16_t COL_RED         = 0xF986; // Error red
constexpr uint16_t COL_TEXT_MAIN   = 0xFFFF;
constexpr uint16_t COL_TEXT_DIM    = 0xAD55;

constexpr int16_t PILL_W = 136;
constexpr int16_t PILL_H = 20;
constexpr int16_t PILL_Y = 6;

bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    return x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh);
}

} // namespace

void UiManager::begin(CydDisplay* lcd, SettingsManager* settings,
                      BackgroundManager* bg, BleHidMouse* ble, TouchpadEngine* touchpad) {
    _lcd      = lcd;
    _settings = settings;
    _bg       = bg;
    _ble      = ble;
    _touchpad = touchpad;

    applyDisplaySettings();
    _lastBleConnected = _ble ? _ble->isConnected() : false;
}

void UiManager::applyDisplaySettings() {
    if (!_lcd || !_settings) return;
    const TouchpadSettings& cfg = _settings->get();

    _lcd->setRotation(cfg.rotation & 0x03);
    _lcd->invertDisplay(cfg.invertColors);
    _lcd->setBrightness(_settings->getBrightnessPwm());

    if (cfg.calibrated) {
        // LovyanGFX stores calData in canonical panel space and automatically applies getRotation()
        uint16_t calCopy[8];
        memcpy(calCopy, cfg.calData, sizeof(calCopy));
        _lcd->setTouchCalibrate(calCopy);
    }
}

void UiManager::runTouchCalibration() {
    if (!_lcd || !_settings) return;
    const uint8_t rot = _settings->get().rotation & 0x03;
    _lcd->setRotation(rot);

    _lcd->fillScreen(COL_BG_DARK);
    _lcd->setTextDatum(textdatum_t::middle_center);
    _lcd->setTextColor(COL_TEXT_MAIN, COL_BG_DARK);
    _lcd->setFont(&fonts::Font2);
    _lcd->drawString("Touch Calibration", _lcd->width() / 2, _lcd->height() / 2 - 18);
    _lcd->setTextColor(COL_TEXT_DIM, COL_BG_DARK);
    _lcd->drawString("Tap each corner arrow", _lcd->width() / 2, _lcd->height() / 2 + 8);

    uint16_t calData[8] = {0};
    _lcd->calibrateTouch(calData, COL_ACCENT, COL_BG_DARK, 18);
    _settings->saveCalibration(calData, rot);
    _lcd->setTouchCalibrate(calData);

    // Wait until finger is lifted before returning
    int16_t tx = 0, ty = 0;
    while (_lcd->getTouch(&tx, &ty)) {
        delay(15);
    }
}

void UiManager::showTouchpad(bool forceBanner) {
    _screen = AppScreen::Touchpad;
    if (_touchpad) _touchpad->reset();

    _bg->drawFull(*_lcd);
    drawTouchpadGuides();

    const OverlayMode ovl = _settings->get().overlayMode;
    if (ovl == OverlayMode::Always || (ovl == OverlayMode::AutoHide && forceBanner)) {
        drawStatusPill();
    } else {
        _pillVisible = false;
    }
}

void UiManager::showBootImportPrompt(const String& candidatePath) {
    _bootCandidate = candidatePath;
    _screen        = AppScreen::BootImportPrompt;
    drawBootPromptScreen();
}

void UiManager::toggleSettings() {
    if (_screen == AppScreen::Touchpad) {
        if (_touchpad) _touchpad->reset();
        if (_ble) _ble->releaseAll();
        _screen       = AppScreen::Settings;
        _settingsPage = 0;
        _uiWasTouched = true; // Ignore any held touch on transition
        drawSettingsScreen();
    } else {
        showTouchpad(true);
    }
}

void UiManager::drawTouchpadGuides() {
    const TouchpadSettings& cfg = _settings->get();
    if (cfg.overlayMode == OverlayMode::Hidden) return;

    const int16_t w = _lcd->width();
    const int16_t h = _lcd->height();
    const uint16_t lineCol = _lcd->color565(85, 108, 136);
    const uint16_t iconCol = _lcd->color565(120, 155, 190);

    const bool hasRight  = (cfg.scrollMode == ScrollMode::RightVertical ||
                            cfg.scrollMode == ScrollMode::RightAndBottomBoth);
    const bool hasLeft   = (cfg.scrollMode == ScrollMode::LeftVertical);
    const bool hasBottom = (cfg.scrollMode == ScrollMode::RightAndBottomBoth);
    const bool hasCornerBtn = (cfg.homeGestureMode == HomeGestureMode::Both ||
                               cfg.homeGestureMode == HomeGestureMode::CornerOnly);
    const bool hasSwipeUp   = (cfg.homeGestureMode == HomeGestureMode::Both ||
                               cfg.homeGestureMode == HomeGestureMode::SwipeOnly);

    _lcd->startWrite();

    if (hasRight || hasLeft) {
        const int16_t sx    = hasRight ? (w - Config::VSCROLL_STRIP_WIDTH) : Config::VSCROLL_STRIP_WIDTH;
        const int16_t vMaxY = (hasBottom || (hasRight && hasCornerBtn))
                            ? (h - Config::HSCROLL_STRIP_HEIGHT)
                            : h;
        const int16_t vcx   = hasRight ? (w - Config::VSCROLL_STRIP_WIDTH / 2)
                                       : (Config::VSCROLL_STRIP_WIDTH / 2);

        for (int16_t y = 6; y <= vMaxY - 5; y += 7) {
            _lcd->drawFastVLine(sx, y, 3, lineCol);
        }
        // Up/down chevrons + centered scroll thumb indicator
        _lcd->drawLine(vcx - 4, 20, vcx, 16, iconCol);
        _lcd->drawLine(vcx + 4, 20, vcx, 16, iconCol);
        _lcd->drawRoundRect(vcx - 2, vMaxY / 2 - 13, 5, 26, 2, lineCol);
        _lcd->drawLine(vcx - 4, vMaxY - 20, vcx, vMaxY - 16, iconCol);
        _lcd->drawLine(vcx + 4, vMaxY - 20, vcx, vMaxY - 16, iconCol);
    }

    if (hasBottom || hasSwipeUp) {
        const int16_t sy    = h - Config::HSCROLL_STRIP_HEIGHT;
        const int16_t hMinX = (cfg.cornerRightClick && (hasRight || hasCornerBtn)) ? Config::CORNER_RCLICK_W : 0;
        const int16_t hMaxX = (hasRight || hasCornerBtn) ? (w - Config::VSCROLL_STRIP_WIDTH) : w;
        const int16_t hMidX = (hMinX + hMaxX) / 2;
        const int16_t hcy   = h - Config::HSCROLL_STRIP_HEIGHT / 2;

        for (int16_t x = hMinX + 6; x <= hMaxX - 5; x += 7) {
            _lcd->drawFastHLine(x, sy, 3, lineCol);
        }
        if (hasBottom) {
            // Left/right chevrons + centered scroll thumb indicator
            _lcd->drawLine(hMinX + 20, hcy - 4, hMinX + 16, hcy, iconCol);
            _lcd->drawLine(hMinX + 20, hcy + 4, hMinX + 16, hcy, iconCol);
            _lcd->drawRoundRect(hMidX - 13, hcy + (hasSwipeUp ? 2 : -2), 26, 5, 2, lineCol);
            _lcd->drawLine(hMaxX - 20, hcy - 4, hMaxX - 16, hcy, iconCol);
            _lcd->drawLine(hMaxX - 20, hcy + 4, hMaxX - 16, hcy, iconCol);
        }
        if (hasSwipeUp) {
            // Subtle upward swipe chevron centered near top of bottom strip
            const int16_t tipY = hasBottom ? (sy + 5) : (hcy - 3);
            _lcd->drawLine(hMidX - 5, tipY + 4, hMidX, tipY, iconCol);
            _lcd->drawLine(hMidX + 5, tipY + 4, hMidX, tipY, iconCol);
        }
    }

    if (hasCornerBtn) {
        const int16_t bx = w - Config::VSCROLL_STRIP_WIDTH;
        const int16_t by = h - Config::HSCROLL_STRIP_HEIGHT;
        // Frame the bottom-right 36x34 corner button cleanly
        for (int16_t x = bx + 3; x <= w - 5; x += 6) {
            _lcd->drawFastHLine(x, by, 3, lineCol);
        }
        for (int16_t y = by + 3; y <= h - 5; y += 6) {
            _lcd->drawFastVLine(bx, y, 3, lineCol);
        }
        // Minimalist Home house icon centered inside the corner box
        const int16_t cx = bx + Config::VSCROLL_STRIP_WIDTH / 2;
        const int16_t cy = by + Config::HSCROLL_STRIP_HEIGHT / 2 + 1;
        _lcd->drawLine(cx - 6, cy - 1, cx, cy - 6, iconCol);
        _lcd->drawLine(cx + 6, cy - 1, cx, cy - 6, iconCol);
        _lcd->drawFastVLine(cx - 4, cy - 1, 7, iconCol);
        _lcd->drawFastVLine(cx + 4, cy - 1, 7, iconCol);
        _lcd->drawFastHLine(cx - 4, cy + 5, 9, iconCol);
    }

    if (cfg.cornerRightClick) {
        bool rightSide = !hasRight && !hasCornerBtn;
        int16_t cx = rightSide ? (w - Config::CORNER_RCLICK_W) : 0;
        int16_t cy = h - Config::CORNER_RCLICK_H;
        for (int16_t x = cx + 4; x < cx + Config::CORNER_RCLICK_W - 4; x += 4) {
            _lcd->drawPixel(x, cy, lineCol);
        }
        for (int16_t y = cy + 4; y < h - 4; y += 4) {
            _lcd->drawPixel(rightSide ? cx : (cx + Config::CORNER_RCLICK_W), y, lineCol);
        }
    }
    _lcd->endWrite();
}

void UiManager::drawStatusPill(const char* overrideText, uint16_t accentColor) {
    if (_settings->get().overlayMode == OverlayMode::Hidden && overrideText == nullptr) {
        _pillVisible = false;
        return;
    }

    const int16_t pillX = (_lcd->width() - PILL_W) / 2;
    const bool connected = _ble && _ble->isConnected();

    uint16_t dotCol = accentColor;
    const char* label = overrideText;
    if (!label) {
        if (connected) {
            label  = (_touchpad && _touchpad->isDragging()) ? "Dragging" : "BLE Connected";
            dotCol = COL_GREEN;
        } else {
            label  = "BLE Pairing...";
            dotCol = COL_AMBER;
        }
    }

    _lcd->fillRoundRect(pillX, PILL_Y, PILL_W, PILL_H, 10, COL_BG_DARK);
    _lcd->drawRoundRect(pillX, PILL_Y, PILL_W, PILL_H, 10, COL_CARD_BORDER);
    _lcd->fillCircle(pillX + 12, PILL_Y + PILL_H / 2, 4, dotCol);

    _lcd->setFont(&fonts::Font0);
    _lcd->setTextDatum(textdatum_t::middle_left);
    _lcd->setTextColor(COL_TEXT_MAIN, COL_BG_DARK);
    _lcd->drawString(label, pillX + 22, PILL_Y + PILL_H / 2);

    _pillVisible = true;
    _pillShownMs = millis();
}

void UiManager::hideStatusPill() {
    if (!_pillVisible) return;
    const int16_t pillX = (_lcd->width() - PILL_W) / 2;
    _bg->drawRect(*_lcd, pillX, PILL_Y, PILL_W, PILL_H);
    drawTouchpadGuides();
    _pillVisible = false;
}

void UiManager::update(bool rawTouched, int16_t touchX, int16_t touchY, GestureEvent tpEvent) {
    const uint32_t now = millis();

    if (_screen == AppScreen::Touchpad) {
        if (tpEvent == GestureEvent::OpenSettings) {
            toggleSettings();
            return;
        }

        const bool connected = _ble && _ble->isConnected();
        const bool dragging  = _touchpad && _touchpad->isDragging();

        if (connected != _lastBleConnected) {
            _lastBleConnected = connected;
            drawStatusPill();
        } else if (dragging != _lastDragging) {
            _lastDragging = dragging;
            if (_settings->get().overlayMode != OverlayMode::Hidden) {
                drawStatusPill();
            }
        }

        if (tpEvent == GestureEvent::RightClick &&
            _settings->get().overlayMode != OverlayMode::Hidden) {
            drawStatusPill("Right Click", COL_ACCENT);
        } else if (tpEvent == GestureEvent::ShortcutTriggered &&
                   _touchpad != nullptr &&
                   _settings->get().overlayMode != OverlayMode::Hidden) {
            drawStatusPill(SettingsManager::shortcutActionLabel(_touchpad->lastShortcutAction()), COL_ACCENT);
        }

        if (_pillVisible && _settings->get().overlayMode == OverlayMode::AutoHide && !dragging) {
            if (now - _pillShownMs >= Config::HUD_AUTOHIDE_MS) {
                hideStatusPill();
            }
        }
        return;
    }

    // Menu tap detection (fire on clean release)
    if (rawTouched) {
        if (!_uiWasTouched) {
            _uiWasTouched  = true;
            _uiTouchDownMs = now;
            _uiDownX       = touchX;
            _uiDownY       = touchY;
        }
    } else if (_uiWasTouched) {
        _uiWasTouched = false;
        if (_uiTouchDownMs > 0 && (now - _uiTouchDownMs) < 800) {
            if (_screen == AppScreen::Settings) {
                handleSettingsTap(_uiDownX, _uiDownY);
            } else if (_screen == AppScreen::SdFilePicker) {
                handleSdPickerTap(_uiDownX, _uiDownY);
            } else if (_screen == AppScreen::BootImportPrompt) {
                handleBootPromptTap(_uiDownX, _uiDownY);
            }
        }
        _uiTouchDownMs = 0;
    }
}

// ---------------------------------------------------------------------------
// Settings Menu (4 clean pages, works in both 320x240 and 240x320)
// ---------------------------------------------------------------------------

void UiManager::drawSettingsScreen() {
    constexpr uint8_t TOTAL_SETTINGS_PAGES = 4;
    const int16_t w = _lcd->width();
    const int16_t h = _lcd->height();
    const TouchpadSettings& cfg = _settings->get();

    _lcd->fillScreen(COL_BG_DARK);

    // Header Bar (height 30)
    _lcd->fillRect(0, 0, w, 30, COL_CARD);
    _lcd->drawFastHLine(0, 30, w, COL_CARD_BORDER);

    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(textdatum_t::middle_left);
    _lcd->setTextColor(COL_TEXT_MAIN, COL_CARD);
    _lcd->drawString(_settingsPage == 1 ? "Corner Actions" : "Settings", 10, 15);

    // Done button top-right
    const int16_t doneW = 58;
    _lcd->fillRoundRect(w - doneW - 6, 4, doneW, 22, 5, COL_ACCENT);
    _lcd->setTextDatum(textdatum_t::middle_center);
    _lcd->setTextColor(COL_BG_DARK, COL_ACCENT);
    _lcd->drawString("Done", w - doneW / 2 - 6, 15);

    // Rows layout (5 rows on Corner Actions page, 4 rows on other pages)
    const int rowsOnPage  = (_settingsPage == 1) ? 5 : 4;
    const int16_t topY    = 36;
    const int16_t bottomH = 32;
    const int16_t availH  = h - topY - bottomH - 4;
    const int16_t rowStep = availH / rowsOnPage;
    const int16_t rowH    = rowStep - 4;

    auto drawRow = [&](int idx, const char* label, const String& value, uint16_t valBg = COL_CARD_BORDER) {
        int16_t ry = topY + idx * rowStep;
        _lcd->fillRoundRect(8, ry, w - 16, rowH, 6, COL_CARD);
        _lcd->drawRoundRect(8, ry, w - 16, rowH, 6, COL_CARD_BORDER);

        _lcd->setFont(&fonts::Font2);
        _lcd->setTextDatum(textdatum_t::middle_left);
        _lcd->setTextColor(COL_TEXT_MAIN, COL_CARD);
        _lcd->drawString(label, 14, ry + rowH / 2);

        if (value.length() > 0) {
            int16_t badgeW   = (w >= 300) ? 116 : 104;
            int16_t badgePad = (rowH < 34) ? 3 : 4;
            int16_t bx       = w - 12 - badgeW;
            _lcd->fillRoundRect(bx, ry + badgePad, badgeW, rowH - badgePad * 2, 5, valBg);
            _lcd->setTextDatum(textdatum_t::middle_center);
            _lcd->setTextColor(COL_TEXT_MAIN, valBg);
            _lcd->drawString(value, bx + badgeW / 2, ry + rowH / 2);
        }
    };

    auto actionBadgeCol = [](ShortcutAction act) -> uint16_t {
        return (act == ShortcutAction::None) ? COL_CARD_BORDER : COL_GREEN;
    };

    if (_settingsPage == 0) {
        drawRow(0, "SD Background", "Import >", COL_ACCENT);
        drawRow(1, "Sensitivity", SettingsManager::sensitivityLabel(cfg.sensitivity));
        drawRow(2, "Scroll Strip", SettingsManager::scrollModeLabel(cfg.scrollMode));
        drawRow(3, "Home Gesture", SettingsManager::homeGestureModeLabel(cfg.homeGestureMode),
                cfg.homeGestureMode != HomeGestureMode::Disabled ? COL_GREEN : COL_CARD_BORDER);
    } else if (_settingsPage == 1) {
        drawRow(0, "Single Click", SettingsManager::shortcutActionLabel(cfg.singleTapAction),
                actionBadgeCol(cfg.singleTapAction));
        drawRow(1, "Double Click", SettingsManager::shortcutActionLabel(cfg.doubleTapAction),
                actionBadgeCol(cfg.doubleTapAction));
        drawRow(2, "Triple Click", SettingsManager::shortcutActionLabel(cfg.tripleTapAction),
                actionBadgeCol(cfg.tripleTapAction));
        drawRow(3, "Long Click", SettingsManager::shortcutActionLabel(cfg.longPressAction),
                actionBadgeCol(cfg.longPressAction));
        drawRow(4, "Hold Time", SettingsManager::longClickLabel(cfg.longClickIdx));
    } else if (_settingsPage == 2) {
        drawRow(0, "Orientation", SettingsManager::rotationLabel(cfg.rotation));
        drawRow(1, "HUD Overlay", SettingsManager::overlayModeLabel(cfg.overlayMode));
        drawRow(2, "Brightness", SettingsManager::brightnessLabel(cfg.brightnessIdx));
        drawRow(3, "Natural Scroll", cfg.naturalScroll ? "On" : "Off",
                cfg.naturalScroll ? COL_GREEN : COL_CARD_BORDER);
    } else {
        drawRow(0, "Invert Colors", cfg.invertColors ? "On" : "Off",
                cfg.invertColors ? COL_GREEN : COL_CARD_BORDER);
        drawRow(1, "Status LED", cfg.statusLed ? "On" : "Off",
                cfg.statusLed ? COL_GREEN : COL_CARD_BORDER);
        drawRow(2, "Touch Calib.", "Run >", COL_ACCENT);
        String bondStr = "Clear (" + String(_ble ? _ble->getBondedCount() : 0) + ")";
        drawRow(3, "BLE Pairings", bondStr, COL_RED);
    }

    // Bottom Navigation Bar + Clear BG shortcut on Page 0
    const int16_t navY = h - bottomH;
    _lcd->drawFastHLine(0, navY, w, COL_CARD_BORDER);

    // Prev button
    _lcd->fillRoundRect(8, navY + 4, 58, 24, 5, COL_CARD);
    _lcd->drawRoundRect(8, navY + 4, 58, 24, 5, COL_CARD_BORDER);
    _lcd->setTextDatum(textdatum_t::middle_center);
    _lcd->setTextColor(COL_TEXT_MAIN, COL_CARD);
    _lcd->drawString("< Prev", 37, navY + 16);

    // Center page indicator (or Reset BG button on page 0 if custom BG exists)
    if (_settingsPage == 0 && _bg->hasCustomBackground()) {
        int16_t rstW = 92;
        int16_t rstX = (w - rstW) / 2;
        _lcd->fillRoundRect(rstX, navY + 4, rstW, 24, 5, COL_CARD);
        _lcd->drawRoundRect(rstX, navY + 4, rstW, 24, 5, COL_RED);
        _lcd->setTextColor(COL_TEXT_MAIN, COL_CARD);
        _lcd->drawString("Clear BG", w / 2, navY + 16);
    } else {
        String pg = String(_settingsPage + 1) + " / " + String(TOTAL_SETTINGS_PAGES);
        _lcd->setTextColor(COL_TEXT_DIM, COL_BG_DARK);
        _lcd->drawString(pg, w / 2, navY + 16);
    }

    // Next button
    _lcd->fillRoundRect(w - 66, navY + 4, 58, 24, 5, COL_CARD);
    _lcd->drawRoundRect(w - 66, navY + 4, 58, 24, 5, COL_CARD_BORDER);
    _lcd->setTextColor(COL_TEXT_MAIN, COL_CARD);
    _lcd->drawString("Next >", w - 37, navY + 16);
}

void UiManager::handleSettingsTap(int16_t x, int16_t y) {
    constexpr uint8_t TOTAL_SETTINGS_PAGES = 4;
    const int16_t w = _lcd->width();
    const int16_t h = _lcd->height();
    TouchpadSettings& cfg = _settings->get();

    // Done button
    if (inRect(x, y, w - 68, 0, 68, 32)) {
        showTouchpad(true);
        return;
    }

    // Bottom Nav bar
    const int16_t bottomH = 32;
    const int16_t navY    = h - bottomH;
    if (y >= navY) {
        if (x < 74) {
            _settingsPage = (_settingsPage + TOTAL_SETTINGS_PAGES - 1) % TOTAL_SETTINGS_PAGES;
            drawSettingsScreen();
        } else if (x > w - 74) {
            _settingsPage = (_settingsPage + 1) % TOTAL_SETTINGS_PAGES;
            drawSettingsScreen();
        } else if (_settingsPage == 0 && _bg->hasCustomBackground()) {
            _bg->clearCustomBackground();
            showBlockingNotice("Background Cleared", "Restored default touchpad grid.", "", COL_ACCENT, 900);
            drawSettingsScreen();
        }
        return;
    }

    // Row tap calculation
    const int rowsOnPage  = (_settingsPage == 1) ? 5 : 4;
    const int16_t topY    = 36;
    const int16_t availH  = h - topY - bottomH - 4;
    const int16_t rowStep = availH / rowsOnPage;
    if (y < topY) return;
    int row = (y - topY) / rowStep;
    if (row < 0 || row >= rowsOnPage) return;

    auto nextAction = [](ShortcutAction act) -> ShortcutAction {
        const uint8_t n = static_cast<uint8_t>(ShortcutAction::COUNT);
        return static_cast<ShortcutAction>((static_cast<uint8_t>(act) + 1) % n);
    };

    if (_settingsPage == 0) {
        switch (row) {
            case 0:
                openSdPicker();
                return;
            case 1:
                cfg.sensitivity = (cfg.sensitivity + 1) % 4;
                break;
            case 2:
                cfg.scrollMode = static_cast<ScrollMode>((static_cast<uint8_t>(cfg.scrollMode) + 1) % 4);
                break;
            case 3:
                cfg.homeGestureMode = static_cast<HomeGestureMode>(
                    (static_cast<uint8_t>(cfg.homeGestureMode) + 1) % 4);
                break;
        }
    } else if (_settingsPage == 1) {
        switch (row) {
            case 0:
                cfg.singleTapAction = nextAction(cfg.singleTapAction);
                break;
            case 1:
                cfg.doubleTapAction = nextAction(cfg.doubleTapAction);
                break;
            case 2:
                cfg.tripleTapAction = nextAction(cfg.tripleTapAction);
                break;
            case 3:
                cfg.longPressAction = nextAction(cfg.longPressAction);
                break;
            case 4:
                cfg.longClickIdx = (cfg.longClickIdx + 1) % LONG_CLICK_OPTIONS_COUNT;
                break;
        }
    } else if (_settingsPage == 2) {
        switch (row) {
            case 0:
                cfg.rotation = (cfg.rotation + 1) & 0x03;
                _settings->save();
                applyDisplaySettings();
                drawSettingsScreen();
                return;
            case 1:
                cfg.overlayMode = static_cast<OverlayMode>((static_cast<uint8_t>(cfg.overlayMode) + 1) % 3);
                break;
            case 2:
                cfg.brightnessIdx = (cfg.brightnessIdx + 1) & 0x03;
                _lcd->setBrightness(_settings->getBrightnessPwm());
                break;
            case 3:
                cfg.naturalScroll = !cfg.naturalScroll;
                break;
        }
    } else {
        switch (row) {
            case 0:
                cfg.invertColors = !cfg.invertColors;
                _lcd->invertDisplay(cfg.invertColors);
                break;
            case 1:
                cfg.statusLed = !cfg.statusLed;
                break;
            case 2:
                runTouchCalibration();
                drawSettingsScreen();
                return;
            case 3:
                if (_ble) _ble->clearBonds();
                showBlockingNotice("Pairings Cleared", "Forgot saved Bluetooth bonds.", "Ready to pair with phone.", COL_ACCENT, 1200);
                drawSettingsScreen();
                return;
        }
    }

    _settings->save();
    drawSettingsScreen();
}

// ---------------------------------------------------------------------------
// SD File Picker & Import Flow
// ---------------------------------------------------------------------------

void UiManager::openSdPicker() {
    _screen  = AppScreen::SdFilePicker;
    _sdPage  = 0;
    _sdError = "";
    _bg->listSdImages(_sdFiles, _sdError);
    drawSdPickerScreen();
}

void UiManager::drawSdPickerScreen() {
    const int16_t w = _lcd->width();
    const int16_t h = _lcd->height();

    _lcd->fillScreen(COL_BG_DARK);

    // Header
    _lcd->fillRect(0, 0, w, 30, COL_CARD);
    _lcd->drawFastHLine(0, 30, w, COL_CARD_BORDER);

    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(textdatum_t::middle_left);
    _lcd->setTextColor(COL_TEXT_MAIN, COL_CARD);
    _lcd->drawString("Select SD Image", 10, 15);

    _lcd->fillRoundRect(w - 64, 4, 58, 22, 5, COL_CARD_BORDER);
    _lcd->setTextDatum(textdatum_t::middle_center);
    _lcd->setTextColor(COL_TEXT_MAIN, COL_CARD_BORDER);
    _lcd->drawString("Back", w - 35, 15);

    if (_sdFiles.empty()) {
        _lcd->setTextDatum(textdatum_t::middle_center);
        _lcd->setTextColor(COL_AMBER, COL_BG_DARK);
        _lcd->drawString(_sdError.isEmpty() ? "No images found" : _sdError, w / 2, h / 2 - 14);
        _lcd->setTextColor(COL_TEXT_DIM, COL_BG_DARK);
        _lcd->drawString("Put .jpg/.png/.bmp on FAT32 SD", w / 2, h / 2 + 12);

        // Retry button
        _lcd->fillRoundRect(w / 2 - 48, h / 2 + 34, 96, 28, 6, COL_ACCENT);
        _lcd->setTextColor(COL_BG_DARK, COL_ACCENT);
        _lcd->drawString("Rescan SD", w / 2, h / 2 + 48);
        return;
    }

    constexpr int FILES_PER_PAGE = 4;
    const int totalPages = (static_cast<int>(_sdFiles.size()) + FILES_PER_PAGE - 1) / FILES_PER_PAGE;
    if (_sdPage >= totalPages) _sdPage = totalPages - 1;
    if (_sdPage < 0) _sdPage = 0;

    const int16_t topY    = 36;
    const int16_t bottomH = 32;
    const int16_t rowStep = (h - topY - bottomH - 4) / FILES_PER_PAGE;
    const int16_t rowH    = rowStep - 4;

    for (int i = 0; i < FILES_PER_PAGE; ++i) {
        size_t idx = static_cast<size_t>(_sdPage * FILES_PER_PAGE + i);
        if (idx >= _sdFiles.size()) break;

        int16_t ry = topY + i * rowStep;
        _lcd->fillRoundRect(8, ry, w - 16, rowH, 6, COL_CARD);
        _lcd->drawRoundRect(8, ry, w - 16, rowH, 6, COL_CARD_BORDER);

        _lcd->setTextDatum(textdatum_t::middle_left);
        _lcd->setTextColor(COL_TEXT_MAIN, COL_CARD);
        String label = _sdFiles[idx];
        if (label.length() > static_cast<unsigned>((w - 36) / 8)) {
            label = "..." + label.substring(label.length() - ((w - 50) / 8));
        }
        _lcd->drawString(label, 16, ry + rowH / 2);
    }

    // Footer pagination
    const int16_t navY = h - bottomH;
    _lcd->drawFastHLine(0, navY, w, COL_CARD_BORDER);
    if (totalPages > 1) {
        _lcd->fillRoundRect(8, navY + 4, 58, 24, 5, COL_CARD);
        _lcd->drawRoundRect(8, navY + 4, 58, 24, 5, COL_CARD_BORDER);
        _lcd->setTextDatum(textdatum_t::middle_center);
        _lcd->setTextColor(COL_TEXT_MAIN, COL_CARD);
        _lcd->drawString("< Prev", 37, navY + 16);

        _lcd->fillRoundRect(w - 66, navY + 4, 58, 24, 5, COL_CARD);
        _lcd->drawRoundRect(w - 66, navY + 4, 58, 24, 5, COL_CARD_BORDER);
        _lcd->drawString("Next >", w - 37, navY + 16);
    }
    _lcd->setTextDatum(textdatum_t::middle_center);
    _lcd->setTextColor(COL_TEXT_DIM, COL_BG_DARK);
    _lcd->drawString("Tap file to save to Flash", w / 2, navY + 16);
}

void UiManager::handleSdPickerTap(int16_t x, int16_t y) {
    const int16_t w = _lcd->width();
    const int16_t h = _lcd->height();

    // Back button
    if (inRect(x, y, w - 70, 0, 70, 32)) {
        _screen = AppScreen::Settings;
        drawSettingsScreen();
        return;
    }

    if (_sdFiles.empty()) {
        if (inRect(x, y, w / 2 - 56, h / 2 + 28, 112, 36)) {
            openSdPicker();
        }
        return;
    }

    constexpr int FILES_PER_PAGE = 4;
    const int totalPages = (static_cast<int>(_sdFiles.size()) + FILES_PER_PAGE - 1) / FILES_PER_PAGE;
    const int16_t bottomH = 32;
    const int16_t navY    = h - bottomH;

    if (y >= navY && totalPages > 1) {
        if (x < 74) {
            _sdPage = (_sdPage + totalPages - 1) % totalPages;
            drawSdPickerScreen();
        } else if (x > w - 74) {
            _sdPage = (_sdPage + 1) % totalPages;
            drawSdPickerScreen();
        }
        return;
    }

    const int16_t topY    = 36;
    const int16_t rowStep = (h - topY - bottomH - 4) / FILES_PER_PAGE;
    if (y < topY || y >= navY) return;

    int row = (y - topY) / rowStep;
    size_t idx = static_cast<size_t>(_sdPage * FILES_PER_PAGE + row);
    if (row >= 0 && row < FILES_PER_PAGE && idx < _sdFiles.size()) {
        executeSdImport(_sdFiles[idx]);
    }
}

void UiManager::executeSdImport(const String& sdPath) {
    String err;
    drawProgressModal("Importing to Flash", 0, sdPath.c_str());

    bool ok = _bg->importFromSd(
        sdPath,
        [this](int pct, const char* stage) {
            drawProgressModal("Importing to Flash", pct, stage);
        },
        err);

    if (ok) {
        showBlockingNotice("Saved to Flash!", "You can now safely remove", "the microSD card.", COL_GREEN, 2000);
        showTouchpad(true);
    } else {
        showBlockingNotice("Import Failed", err.c_str(), "Tap or wait to return.", COL_RED, 2600);
        if (_screen == AppScreen::BootImportPrompt) {
            showTouchpad(true);
        } else {
            drawSdPickerScreen();
        }
    }
}

void UiManager::drawProgressModal(const char* title, int percent, const char* subtitle) {
    const int16_t w = _lcd->width();
    const int16_t h = _lcd->height();
    const int16_t cardW = w - 28;
    const int16_t cardH = 104;
    const int16_t cx    = 14;
    const int16_t cy    = (h - cardH) / 2;

    if (percent == 0) {
        _lcd->fillScreen(COL_BG_DARK);
        _lcd->fillRoundRect(cx, cy, cardW, cardH, 8, COL_CARD);
        _lcd->drawRoundRect(cx, cy, cardW, cardH, 8, COL_CARD_BORDER);
    }

    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(textdatum_t::middle_center);
    _lcd->setTextColor(COL_TEXT_MAIN, COL_CARD);
    _lcd->drawString(title, w / 2, cy + 22);

    // Clear subtitle band
    _lcd->fillRect(cx + 8, cy + 38, cardW - 16, 20, COL_CARD);
    _lcd->setTextColor(COL_TEXT_DIM, COL_CARD);
    _lcd->drawString(subtitle ? subtitle : "", w / 2, cy + 48);

    // Progress bar
    const int16_t barW = cardW - 32;
    const int16_t barX = cx + 16;
    const int16_t barY = cy + 68;
    _lcd->drawRoundRect(barX, barY, barW, 16, 4, COL_CARD_BORDER);
    int16_t fillW = ((barW - 4) * constrain(percent, 0, 100)) / 100;
    if (fillW > 0) {
        _lcd->fillRoundRect(barX + 2, barY + 2, fillW, 12, 3, COL_ACCENT);
    }
}

void UiManager::showBlockingNotice(const char* title, const char* line1, const char* line2,
                                   uint16_t accentColor, uint32_t autoCloseMs) {
    const int16_t w = _lcd->width();
    const int16_t h = _lcd->height();
    const int16_t cardW = w - 24;
    const int16_t cardH = 108;
    const int16_t cx    = 12;
    const int16_t cy    = (h - cardH) / 2;

    _lcd->fillScreen(COL_BG_DARK);
    _lcd->fillRoundRect(cx, cy, cardW, cardH, 8, COL_CARD);
    _lcd->drawRoundRect(cx, cy, cardW, cardH, 8, accentColor);

    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(textdatum_t::middle_center);
    _lcd->setTextColor(accentColor, COL_CARD);
    _lcd->drawString(title, w / 2, cy + 26);

    _lcd->setTextColor(COL_TEXT_MAIN, COL_CARD);
    if (line1 && line1[0]) _lcd->drawString(line1, w / 2, cy + 56);
    _lcd->setTextColor(COL_TEXT_DIM, COL_CARD);
    if (line2 && line2[0]) _lcd->drawString(line2, w / 2, cy + 78);

    // Wait until autoCloseMs or user tap
    uint32_t start = millis();
    int16_t tx = 0, ty = 0;
    while (_lcd->getTouch(&tx, &ty)) delay(15); // release previous tap
    while (autoCloseMs == 0 || (millis() - start < autoCloseMs)) {
        if (_lcd->getTouch(&tx, &ty)) {
            while (_lcd->getTouch(&tx, &ty)) delay(15);
            break;
        }
        delay(20);
    }
}

// ---------------------------------------------------------------------------
// Boot Auto-Detected SD Image Prompt
// ---------------------------------------------------------------------------

void UiManager::drawBootPromptScreen() {
    const int16_t w = _lcd->width();
    const int16_t h = _lcd->height();

    _lcd->fillScreen(COL_BG_DARK);

    const int16_t cardW = w - 20;
    const int16_t cardH = 146;
    const int16_t cx    = 10;
    const int16_t cy    = (h - cardH) / 2;

    _lcd->fillRoundRect(cx, cy, cardW, cardH, 8, COL_CARD);
    _lcd->drawRoundRect(cx, cy, cardW, cardH, 8, COL_ACCENT);

    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(textdatum_t::middle_center);
    _lcd->setTextColor(COL_TEXT_MAIN, COL_CARD);
    _lcd->drawString("SD Background Found", w / 2, cy + 24);

    _lcd->setTextColor(COL_ACCENT, COL_CARD);
    _lcd->drawString(_bootCandidate, w / 2, cy + 50);

    _lcd->setTextColor(COL_TEXT_DIM, COL_CARD);
    _lcd->drawString("Save to internal flash?", w / 2, cy + 74);

    const int16_t btnW = (cardW - 30) / 2;
    const int16_t btnY = cy + 98;
    _lcd->fillRoundRect(cx + 10, btnY, btnW, 34, 6, COL_ACCENT);
    _lcd->setTextColor(COL_BG_DARK, COL_ACCENT);
    _lcd->drawString("Import", cx + 10 + btnW / 2, btnY + 17);

    _lcd->fillRoundRect(cx + 20 + btnW, btnY, btnW, 34, 6, COL_CARD_BORDER);
    _lcd->setTextColor(COL_TEXT_MAIN, COL_CARD_BORDER);
    _lcd->drawString("Skip", cx + 20 + btnW + btnW / 2, btnY + 17);
}

void UiManager::handleBootPromptTap(int16_t x, int16_t y) {
    const int16_t w     = _lcd->width();
    const int16_t h     = _lcd->height();
    const int16_t cardW = w - 20;
    const int16_t cardH = 146;
    const int16_t cx    = 10;
    const int16_t cy    = (h - cardH) / 2;
    const int16_t btnW  = (cardW - 30) / 2;
    const int16_t btnY  = cy + 98;

    if (inRect(x, y, cx + 10, btnY, btnW, 34)) {
        executeSdImport(_bootCandidate);
    } else if (inRect(x, y, cx + 20 + btnW, btnY, btnW, 34)) {
        showTouchpad(true);
    }
}
