# CYD-BLE-Touchpad

Turn an **ESP32-2432S028 ("CYD2USB" — Dual USB-C + Micro-USB, 2.8" ST7789 TFT)** into a standalone **Bluetooth Low Energy (BLE) HID Touchpad Mouse** tailored for smartphones (**iOS / iPadOS** via AssistiveTouch and **Android**), featuring custom background artwork imported from a microSD card and persisted directly in internal flash so the microSD card can be removed after setup.

---

## Key Features

- **True BLE HID Mouse + Consumer Control (`HOGP`)**
  - Advertises as **`CYD Mouse`** with standard BLE Mouse GAP Appearance (`0x03C2`), Boot + Report Protocol support, NVS bonding, and Identity Resolving Key (`IRK`) distribution for instant auto-reconnect.
  - **Dual HID Reports**:
    - **Report ID 1 (Mouse)**: 5 buttons, 8-bit relative `X` / `Y`, vertical `Wheel`, and horizontal `AC Pan` (`0x0238`).
    - **Report ID 2 (Consumer Control `0x0C`)**: Native system shortcuts (**Home**, **App Switcher**, **Spotlight Search**, **Siri / Voice Assistant**, **Lock Screen**, **Play / Pause**) without declaring a HID Keyboard collection—so **iOS keeps the on-screen soft keyboard visible**.
- **Resistive-Tuned Single-Finger Gesture Engine**
  - Built specifically around the physics of the CYD's 4-wire `XPT2046` resistive panel: median-of-3 spike rejection, exponential moving average (`EMA`) smoothing, sub-pixel remainder accumulation, velocity-based pointer acceleration, and state-aware contact/release debouncing.
  - **Native iOS Tap-and-Drag (`touch → release → touch → drag`)** engineered to work seamlessly with iOS AssistiveTouch (`Perform Touch Gestures = ON`) without falsely triggering Home Screen icon-jiggle / long-press mode.
- **Dedicated Home Button & Bottom Swipe-Up Bar**
  - **Bottom-Right Action Corner** with independent, customizable actions for **Single Click**, **Double Click**, **Triple Click**, and **Long Click** (with configurable hold duration, defaulting to `1.0s`).
  - **Bottom-Edge Swipe-Up** gesture: quick upward flick returns **Home**; swipe up and pause (`0.35s`) opens the **App Switcher**.
- **Edge Scroll Bars**
  - **Vertical Scroll Strip** (`36 px` right or left edge) for smooth continuous list and web scrolling.
  - **Discrete Page-Step Horizontal Bar** (`34 px` bottom edge) tuned so each horizontal swipe switches **one** Home Screen page in that direction rather than overshooting across multiple screens.
- **SD-to-Flash Persistent Custom Backgrounds**
  - Reads `.jpg`, `.jpeg`, `.png`, and `.bmp` images from any FAT32 microSD card.
  - Aspect-fills and pre-renders both **Portrait (`240×320`)** and **Landscape (`320×240`)** RGB565 frames into a `2 MB` internal `LittleFS` flash partition (`300 KB` total footprint).
  - Remove the microSD card once imported—backgrounds stream to the display in `< 30 ms` on boot and switch orientations instantaneously.
- **Zero-Clutter Hardware Settings Trigger**
  - Hold the physical **`BOOT` button (`GPIO 0`) for `1.5 s`** to open or close the 4-page on-device Settings menu, keeping the touchpad surface free of accidental menu hotzones.

---

## Touchpad Layout & Gestures

Default orientation is **Portrait (`240×320`)** (rotatable to Landscape, Portrait Flip, or Landscape Flip in Settings):

```text
+-----------------------------------+
|          [ Status Pill ]          |  <- Auto-hides after 4s (configurable)
|                                   |
|                                   |
|                                   |
|           Main Touchpad           |  :|  <- Right Vertical
|       (Move / Tap / 2x Tap /      |  :|     Scroll Strip (36 px)
|       Tap-to-Drag / Hold RClick)  |  :|
|                                   |  :|
|                                   |  :|
|                                   |  :|
+----------------------------+------+---+
|  <   ^ Swipe Up = Home   > |  [Home]  |
|  Horizontal Page Bar (34px)|  Corner  |
+----------------------------+----------+
```

### Main Touchpad Gestures

| Gesture | Action | Notes |
| :--- | :--- | :--- |
| **Slide 1 Finger** | Move Cursor | Velocity-accelerated with 1 px slow-speed precision |
| **Quick Tap** | Left Click | Crisp tap (`< 500 ms`), works immediately after swiping |
| **Double Tap** | Double Click | Two quick taps in the main pad area |
| **Tap → Release → Touch & Drag** | Click & Drag / Swipe | Touch, lift, then touch anywhere on the pad within `400 ms` and slide |
| **Stationary Press & Hold** | Right Click | Hold still for the configured **Hold Time** (`1.0s` default) |

### Edge Bars & Bottom-Right Action Corner

| Zone / Input | Default Action | Notes |
| :--- | :--- | :--- |
| **Right (or Left) Strip Slide** | Vertical Scroll | Continuous mouse wheel scrolling |
| **Bottom Strip Left / Right Swipe** | Next / Prev Page (`AC Pan`) | Discrete **1 page step per swipe** (reverse `>= 26 px` mid-stroke to step back) |
| **Bottom Strip Swipe Up** | **Home Screen** | Flick upward from the bottom strip into the pad |
| **Bottom Strip Swipe Up + Hold** | **App Switcher** | Swipe upward and pause finger for `0.35s` |
| **Corner Button — Single Click** | **Home Screen** | Configurable in **Settings → Corner Actions** |
| **Corner Button — Double Click** | **App Switcher** | Configurable in **Settings → Corner Actions** |
| **Corner Button — Triple Click** | **Spotlight Search** | Configurable in **Settings → Corner Actions** |
| **Corner Button — Long Click** | **Lock Screen** | Configurable action + **Hold Time** (`0.5s`–`2.0s`, `1.0s` default) |
| **Hardware `BOOT` Button (`1.5s`)** | Open / Close Settings | Physical `GPIO 0` button on the back of the CYD |

---

## Hardware & SPI Architecture

### Target Board
- **Board**: `ESP32-2432S028` **"CYD2USB"** (2.8" `240×320` `ST7789` TFT, `XPT2046` resistive touch, USB-C + Micro-USB, onboard microSD slot, active-low RGB LED).
- *Note for older ILI9341 single-USB CYD boards*: If your panel colors appear inverted, toggle **Invert Colors** directly on Page 4 of the on-device Settings menu.

### Zero-Contention 3-SPI-Bus Allocation
The ESP32-WROOM-32 provides two free hardware SPI hosts (`HSPI` and `VSPI`), while the CYD wires Display, Touch, and MicroSD to three separate pin groups:
1. **Display (`ST7789`)** → Hardware **`HSPI`** (`55 MHz` DMA)
2. **MicroSD Slot** → Hardware **`VSPI`** (`20 MHz`)
3. **Touch (`XPT2046`)** → `LovyanGFX` dedicated **Software SPI** (`spi_host = -1`, `1 MHz`)

| Peripheral | Bus | Signal | GPIO | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **TFT Display (`ST7789`)** | Hardware `HSPI` | `MOSI` / `MISO` / `SCLK` | `13` / `12` / `14` | `BGR` color order |
| | | `CS` / `DC` / `RST` | `15` / `2` / `-1` | `RST` tied to board `EN` |
| | | `BL` (Backlight) | `21` | `12 kHz` PWM (`25%`–`100%`) |
| **Touch (`XPT2046`)** | Software SPI | `MOSI` / `MISO` / `CLK` | `32` / `39` / `25` | `GPIO 39` input-only |
| | | `CS` / `IRQ` | `33` / `36` | `GPIO 36` input-only |
| **MicroSD Card** | Hardware `VSPI` | `MOSI` / `MISO` / `SCLK` / `CS` | `23` / `19` / `18` / `5` | FAT32 formatted |
| **Status RGB LED** | GPIO | `R` / `G` / `B` | `4` / `16` / `17` | Active-low (`LOW` = ON) |
| **Settings Button** | GPIO | `BOOT` | `0` | Active-low (`1.5s` hold) |

---

## Getting Started

### 1. Build & Flash with PlatformIO

Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI), then build and upload:

```bash
# Compile firmware
pio run

# Upload firmware over USB
pio run -t upload

# (Optional) View 115200-baud gesture telemetry
pio device monitor
```

> **CYD2USB USB-C Tip:** Many CYD2USB boards only wire USB `D+/D-` data lines on **one side** of the USB-C receptacle. If your board powers on when plugged in via USB-C but no serial port (`/dev/cu.wchusbserial*` / `COMx` / `/dev/ttyUSB*`) appears on your computer, **flip the USB-C plug 180°** and reconnect.

### 2. First-Boot Touch Calibration
On the very first boot, the firmware automatically launches a **4-corner Touch Calibration** screen. Tap each corner arrow accurately; calibration constants are stored in NVS flash and automatically transformed across all 4 screen orientations.

### 3. Pair with Your Phone

#### iPhone / iPad (iOS / iPadOS)
1. Enable AssistiveTouch on your iPhone/iPad:
   - Open **Settings → Accessibility → Touch → AssistiveTouch** and toggle **AssistiveTouch ON**.
   - Ensure **Perform Touch Gestures** is toggled **ON** (default) so drag gestures swipe pages and scroll views naturally.
2. Open **Settings → Bluetooth** and pair with **`CYD Mouse`**.
   - Keep default button mappings (do not override Primary Button 1, or native tap-and-drag will be intercepted).

#### Android / macOS / Windows / Linux
1. Open **Bluetooth Settings** and pair with **`CYD Mouse`**.
2. Standard pointer movement, clicks, scroll bars, and media/system consumer shortcuts work out of the box.

---

## Custom Background Import (microSD → Internal Flash)

1. Format a microSD card as **FAT32** and copy `.jpg`, `.jpeg`, `.png`, or `.bmp` artwork onto it:
   - **Auto-Prompt at Boot**: Place `/bg.jpg` (or `/bg.png` / `/bg.bmp` / `/background.*`) in the root directory and boot with the card inserted.
   - **File Picker Anytime**: Place up to 24 images in `/` or `/backgrounds/`, hold **`BOOT` (`1.5s`) → SD Background → Import >**, and tap any file.
   - *(Note: For high-resolution photos `> 1024 px`, prefer `.jpg` or `.bmp` so decoding stays well within ESP32 SRAM.)*
2. Wait a few seconds while the firmware center-crops and writes both Landscape and Portrait RGB565 images to internal `LittleFS`.
3. Once **"Saved to Flash! You can now safely remove the microSD card."** appears, eject the microSD card—your wallpaper persists across power cycles and orientation changes.

---

## On-Device Settings Menu

Hold the rear **`BOOT` button (`GPIO 0`) for `1.5 s`** to open or close Settings (`4` pages):

- **Page 1 — General & Layout**
  - **SD Background**: Browse & import images from microSD *(plus a **Clear BG** button in the footer whenever a custom background is active)*
  - **Sensitivity**: `Low` / `Medium` (default) / `High` / `Max`
  - **Scroll Strip**: `Right + Bottom` (default) / `Right Edge` / `Left Edge` / `Disabled`
  - **Home Gesture**: `Both` (default — Corner Button + Swipe Up) / `Corner Btn` / `Swipe Up` / `Off`
- **Page 2 — Corner Actions**
  - **Single Click**: `Home` (default) / `App Switch` / `Spotlight` / `Siri` / `Lock Screen` / `Play/Pause` / `Right Click` / `Mid Click` / `Button 4` / `Button 5` / `CYD Menu` / `None`
  - **Double Click**: `App Switch` (default) / *(same action list)*
  - **Triple Click**: `Spotlight` (default) / *(same action list)*
  - **Long Click**: `Lock Screen` (default) / *(same action list)*
  - **Hold Time**: `0.5s` / `0.75s` / **`1.0s` (default)** / `1.5s` / `2.0s` *(applies to both Corner Long Click and main-pad stationary Right Click)*
- **Page 3 — Display & Scrolling**
  - **Orientation**: `Portrait` (default) / `Landscape` / `Portrait Flip` / `Landscape Flip`
  - **HUD Overlay**: `Auto-Hide` (default) / `Always On` / `Hidden`
  - **Brightness**: `25%` / `50%` / `75%` / `100%` (default)
  - **Natural Scroll**: `Off` (default) / `On`
- **Page 4 — Hardware & Bluetooth**
  - **Invert Colors**: `Off` / `On` *(for ST7789 panel batch variations)*
  - **Status LED**: `On` (default) / `Off`
  - **Touch Calib.**: Re-run 4-corner resistive calibration
  - **BLE Pairings**: Clear saved NVS Bluetooth bonds to pair fresh

---

## Project Structure

```text
CYD-BLE-Touchpad/
├── include/
│   ├── BackgroundManager.h   # SD image scanner + strip-based LittleFS RGB565 converter
│   ├── BleHidMouse.h         # NimBLE HOGP Mouse (Report ID 1) + Consumer Control (Report ID 2)
│   ├── Config.h              # CYD2USB pinout, geometry, and timing constants
│   ├── CydDisplay.h          # LovyanGFX ST7789 (HSPI) + XPT2046 (Soft-SPI) configuration
│   ├── SettingsManager.h     # NVS persistence & schema migration for settings + calibration
│   ├── TouchpadEngine.h      # Resistive filtering, tap-to-drag, scroll, & shortcut state machine
│   └── UiManager.h           # Touchpad HUD guides, status pill, 4-page Settings, & SD file picker
├── src/
│   ├── BackgroundManager.cpp
│   ├── BleHidMouse.cpp
│   ├── SettingsManager.cpp
│   ├── TouchpadEngine.cpp
│   ├── UiManager.cpp
│   └── main.cpp              # 125 Hz input/UI loop, BOOT button listener, & RGB status LED
├── partitions.csv            # 4 MB Flash layout (~1.9 MB App + 2.0 MB LittleFS + NVS)
└── platformio.ini            # PlatformIO build configuration (ESP32 Arduino + LovyanGFX + NimBLE)
```
