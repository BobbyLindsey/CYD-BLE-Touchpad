#include "BleHidMouse.h"

#if defined(CONFIG_NIMBLE_CPP_IDF)
#  include "services/gap/ble_svc_gap.h"
#  include "services/gatt/ble_svc_gatt.h"
#else
#  include "nimble/nimble/host/services/gap/include/services/gap/ble_svc_gap.h"
#  include "nimble/nimble/host/services/gatt/include/services/gatt/ble_svc_gatt.h"
#endif

namespace {

// Standard BLE HOGP Mouse Report Map (Report ID 1):
// 5 buttons + X/Y + Vertical Wheel + Horizontal AC Pan
const uint8_t kMouseReportDescriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)

    // Buttons 1..5
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (Button 1)
    0x29, 0x05,        //     Usage Maximum (Button 5)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x05,        //     Report Count (5)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    // 3-bit constant padding
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x03,        //     Report Size (3)
    0x81, 0x03,        //     Input (Const,Var,Abs)

    // Relative X, Y, Vertical Wheel
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data,Var,Rel)

    // Relative Horizontal Pan (Consumer Page -> AC Pan)
    0x05, 0x0C,        //     Usage Page (Consumer)
    0x0A, 0x38, 0x02,  //     Usage (AC Pan)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data,Var,Rel)

    0xC0,              //   End Collection (Physical)
    0xC0,              // End Collection (Application)

    // Report ID 2: 16-bit Consumer Control (Home, App Switcher, Spotlight, Siri, Lock, Media)
    // Uses Consumer Page (0x0C) rather than Keyboard (0x01/0x06) so iOS keeps on-screen keyboard visible
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (1023)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data,Arr,Abs)
    0xC0               // End Collection (Application)
};

int8_t clampInt8(int16_t val) {
    if (val > 127)  return 127;
    if (val < -127) return -127;
    return static_cast<int8_t>(val);
}

} // namespace

void BleHidMouse::begin(const char* deviceName, const char* manufacturer) {
    NimBLEDevice::init(deviceName);
    // Mandatory HOGP GAP Appearance (0x2A01 = 0x03C2 HID_MOUSE) so iOS AssistiveTouch
    // classifies this peripheral as a true Bluetooth Mouse with native primary-button drag
    ble_svc_gap_device_appearance_set(HID_MOUSE);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    // Enable bonding + Secure Connections + IRK distribution for iOS/Android RPA reconnect
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

    _server = NimBLEDevice::createServer();
    _server->setCallbacks(this);
    _server->advertiseOnDisconnect(true);

    _hid = new NimBLEHIDDevice(_server);
    _inputMouse    = _hid->inputReport(1); // Matches Report ID 1 (Mouse)
    _inputConsumer = _hid->inputReport(2); // Matches Report ID 2 (Consumer Control)
    // Standard HOGP Boot Mouse Input Report (0x2A33) required alongside Protocol Mode (0x2A4E)
    _bootMouse = _hid->hidService()->createCharacteristic(
        static_cast<uint16_t>(0x2A33),
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC);

    _hid->manufacturer()->setValue(manufacturer);
    _hid->pnp(0x02, 0xE502, 0xA113, 0x0230);
    _hid->hidInfo(0x00, 0x03); // RemoteWake | NormallyConnectable
    _hid->setBatteryLevel(100);
    _hid->reportMap(const_cast<uint8_t*>(kMouseReportDescriptor), sizeof(kMouseReportDescriptor));
    _hid->startServices();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(HID_MOUSE);
    adv->addServiceUUID(_hid->hidService()->getUUID());
    adv->setScanResponse(true);
    // Fast advertising intervals for quick discovery & reconnect
    adv->setMinPreferred(0x06); // 7.5 ms
    adv->setMaxPreferred(0x0C); // 15 ms
    adv->start();

    _lastStateChangeMs = millis();
}

int BleHidMouse::getBondedCount() const {
    return NimBLEDevice::getNumBonds();
}

void BleHidMouse::move(int16_t dx, int16_t dy, int8_t wheel, int8_t pan) {
    if (!_connected) return;
    if (dx == 0 && dy == 0 && wheel == 0 && pan == 0) return;

    // Split large deltas across reports if > 127
    do {
        int8_t stepX = clampInt8(dx);
        int8_t stepY = clampInt8(dy);
        sendReport(stepX, stepY, wheel, pan);
        dx -= stepX;
        dy -= stepY;
        wheel = 0;
        pan   = 0;
    } while (dx != 0 || dy != 0);
}

void BleHidMouse::click(uint8_t button) {
    if (!_connected) return;
    _buttons |= button;
    sendReport(0, 0, 0, 0);
    delay(18);
    _buttons &= ~button;
    sendReport(0, 0, 0, 0);
}

void BleHidMouse::press(uint8_t button) {
    uint8_t next = _buttons | button;
    if (next == _buttons) return;
    _buttons = next;
    if (_connected) {
        sendReport(0, 0, 0, 0);
    }
}

void BleHidMouse::release(uint8_t button) {
    uint8_t next = _buttons & ~button;
    if (next == _buttons) return;
    _buttons = next;
    if (_connected) {
        sendReport(0, 0, 0, 0);
    }
}

void BleHidMouse::releaseAll() {
    if (_buttons != 0) {
        _buttons = 0;
        if (_connected) {
            sendReport(0, 0, 0, 0);
        }
    }
    if (_connected && _inputConsumer != nullptr) {
        uint8_t up[2] = {0x00, 0x00};
        _inputConsumer->setValue(up, sizeof(up));
        _inputConsumer->notify();
    }
}

void BleHidMouse::consumerClick(uint16_t usage) {
    if (!_connected || _inputConsumer == nullptr || usage == 0) return;
    uint8_t down[2] = {
        static_cast<uint8_t>(usage & 0xFF),
        static_cast<uint8_t>((usage >> 8) & 0xFF)
    };
    _inputConsumer->setValue(down, sizeof(down));
    _inputConsumer->notify();
    delay(45);
    uint8_t up[2] = {0x00, 0x00};
    _inputConsumer->setValue(up, sizeof(up));
    _inputConsumer->notify();
}

void BleHidMouse::doubleHome() {
    if (!_connected || _inputConsumer == nullptr) return;
    consumerClick(CONSUMER_MENU);
    delay(115);
    consumerClick(CONSUMER_MENU);
}

void BleHidMouse::clearBonds() {
    _buttons = 0;
    if (_server && _connected) {
        auto peers = _server->getPeerDevices();
        for (uint16_t connHandle : peers) {
            _server->disconnect(connHandle);
        }
        delay(60);
    }
    NimBLEDevice::deleteAllBonds();
    _connected = false;
    _lastStateChangeMs = millis();
    NimBLEDevice::startAdvertising();
}

void BleHidMouse::onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
    _buttons = 0;
    _connected = true;
    _lastStateChangeMs = millis();
    // Request low-latency BLE HID interval (15ms - 18.75ms, 0 latency, 4000ms supervision timeout)
    if (desc != nullptr) {
        pServer->updateConnParams(desc->conn_handle, 12, 15, 0, 400);
    }
}

void BleHidMouse::onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
    (void)pServer;
    (void)desc;
    _buttons = 0;
    _connected = false;
    _lastStateChangeMs = millis();
}

void BleHidMouse::sendReport(int8_t x, int8_t y, int8_t wheel, int8_t pan) {
    if (!_connected || _inputMouse == nullptr) return;

    uint8_t protoMode = 0x01;
    if (_hid != nullptr && _hid->protocolMode() != nullptr) {
        NimBLEAttValue val = _hid->protocolMode()->getValue();
        if (val.size() >= 1) {
            protoMode = val.data()[0];
        }
    }

    if (protoMode == 0x00 && _bootMouse != nullptr) {
        uint8_t bootReport[3] = {_buttons, static_cast<uint8_t>(x), static_cast<uint8_t>(y)};
        _bootMouse->setValue(bootReport, sizeof(bootReport));
        _bootMouse->notify();
        return;
    }

    HidMouseReport report{_buttons, x, y, wheel, pan};
    _inputMouse->setValue(reinterpret_cast<uint8_t*>(&report), sizeof(report));
    _inputMouse->notify();
}

