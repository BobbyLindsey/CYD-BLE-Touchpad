#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

constexpr uint8_t MOUSE_BTN_LEFT    = 0x01;
constexpr uint8_t MOUSE_BTN_RIGHT   = 0x02;
constexpr uint8_t MOUSE_BTN_MIDDLE  = 0x04;
constexpr uint8_t MOUSE_BTN_BACK    = 0x08;
constexpr uint8_t MOUSE_BTN_FORWARD = 0x10;

// USB HID Consumer Page (0x0C) 16-bit Usages supported natively by iOS / Android
constexpr uint16_t CONSUMER_POWER      = 0x0030; // Lock / Wake Screen
constexpr uint16_t CONSUMER_MENU       = 0x0040; // Apple iOS/iPadOS Home Button (1x = Home, 2x = App Switcher)
constexpr uint16_t CONSUMER_PLAY_PAUSE = 0x00CD; // Play / Pause
constexpr uint16_t CONSUMER_VOICE_CMD  = 0x00CF; // Siri / Voice Assistant
constexpr uint16_t CONSUMER_MUTE       = 0x00E2; // Mute
constexpr uint16_t CONSUMER_VOL_UP     = 0x00E9; // Volume Up
constexpr uint16_t CONSUMER_VOL_DOWN   = 0x00EA; // Volume Down
constexpr uint16_t CONSUMER_AC_SEARCH  = 0x0221; // Spotlight Search
constexpr uint16_t CONSUMER_AC_HOME    = 0x0223; // Browser / Android AC Home

struct __attribute__((packed)) HidMouseReport {
    uint8_t buttons;
    int8_t  x;
    int8_t  y;
    int8_t  wheel;
    int8_t  pan;
};

class BleHidMouse : public NimBLEServerCallbacks {
public:
    void begin(const char* deviceName, const char* manufacturer);

    bool isConnected() const { return _connected; }
    uint32_t lastStateChangeMs() const { return _lastStateChangeMs; }
    int getBondedCount() const;

    void move(int16_t dx, int16_t dy, int8_t wheel = 0, int8_t pan = 0);
    void click(uint8_t button = MOUSE_BTN_LEFT);
    void press(uint8_t button = MOUSE_BTN_LEFT);
    void release(uint8_t button = MOUSE_BTN_LEFT);
    void releaseAll();

    // Consumer Control (Report ID 2) system shortcuts (does not hide iOS soft keyboard)
    void consumerClick(uint16_t usage);
    void doubleHome();

    uint8_t pressedButtons() const { return _buttons; }

    // Clears all saved phone pairings in NVS and restarts advertising
    void clearBonds();

    // NimBLEServerCallbacks
    void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override;
    void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override;

private:
    void sendReport(int8_t x, int8_t y, int8_t wheel, int8_t pan);

    NimBLEServer*         _server        = nullptr;
    NimBLEHIDDevice*      _hid           = nullptr;
    NimBLECharacteristic* _inputMouse    = nullptr;
    NimBLECharacteristic* _inputConsumer = nullptr;
    NimBLECharacteristic* _bootMouse     = nullptr;

    volatile bool     _connected         = false;
    volatile uint32_t _lastStateChangeMs = 0;
    uint8_t           _buttons           = 0;
};

