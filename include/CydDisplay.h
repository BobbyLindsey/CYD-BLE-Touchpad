#pragma once

#include <FS.h>
#ifndef LGFX_USE_V1
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>
#include "Config.h"

/**
 * LovyanGFX display & touch configuration tailored for the CYD2USB
 * (ESP32-2432S028 with USB-C + Micro-USB, ST7789 TFT + XPT2046 Touch).
 *
 * - Display uses dedicated Hardware HSPI (SPI2_HOST) at 55 MHz.
 * - Touch uses Software SPI (spi_host = -1) on pins 25/32/39/33/36, leaving
 *   Hardware VSPI (SPI3_HOST) completely free for the MicroSD card slot.
 */
class CydDisplay : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789  _panel_instance;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;
    lgfx::Touch_XPT2046 _touch_instance;

public:
    CydDisplay() {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host    = HSPI_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 55000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = Config::PIN_TFT_SCLK;
            cfg.pin_mosi    = Config::PIN_TFT_MOSI;
            cfg.pin_miso    = Config::PIN_TFT_MISO;
            cfg.pin_dc      = Config::PIN_TFT_DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs           = Config::PIN_TFT_CS;
            cfg.pin_rst          = Config::PIN_TFT_RST;
            cfg.pin_busy         = -1;
            cfg.panel_width      = 240;
            cfg.panel_height     = 320;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = true;
            cfg.invert           = false; // Controlled at runtime via invertDisplay()
            cfg.rgb_order        = false; // BGR color order for CYD2USB ST7789
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel_instance.config(cfg);
        }

        {
            auto cfg = _light_instance.config();
            cfg.pin_bl      = Config::PIN_TFT_BL;
            cfg.invert      = false;
            cfg.freq        = 12000;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        {
            auto cfg = _touch_instance.config();
            cfg.x_min           = 300;
            cfg.x_max           = 3900;
            cfg.y_min           = 200;
            cfg.y_max           = 3700;
            cfg.pin_int         = Config::PIN_TOUCH_IRQ;
            cfg.pin_cs          = Config::PIN_TOUCH_CS;
            cfg.bus_shared      = false;
            cfg.offset_rotation = 0;
            cfg.spi_host        = -1; // Software SPI avoids VSPI conflict with SD card
            cfg.freq            = 1000000;
            cfg.pin_sclk        = Config::PIN_TOUCH_SCLK;
            cfg.pin_mosi        = Config::PIN_TOUCH_MOSI;
            cfg.pin_miso        = Config::PIN_TOUCH_MISO;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }
};
