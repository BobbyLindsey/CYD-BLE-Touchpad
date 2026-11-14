#include "BackgroundManager.h"
#include <LittleFS.h>
#include <SD.h>
#include <algorithm>
#include <math.h>
#include "Config.h"

namespace {

constexpr size_t EXPECTED_BG_BYTES = 320UL * 240UL * 2UL; // 153,600 bytes

bool hasSupportedExtension(const String& name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
           lower.endsWith(".png") || lower.endsWith(".bmp");
}

bool isHiddenOrMacMetadata(const String& fullPath) {
    int slash = fullPath.lastIndexOf('/');
    String base = (slash >= 0) ? fullPath.substring(slash + 1) : fullPath;
    // Ignore hidden files and macOS Finder AppleDouble resource forks ("._filename")
    return base.length() == 0 || base.startsWith(".");
}

uint16_t readBE16(File& f) {
    int b0 = f.read();
    int b1 = f.read();
    if (b0 < 0 || b1 < 0) return 0;
    return static_cast<uint16_t>((b0 << 8) | b1);
}

uint32_t readBE32(File& f) {
    uint32_t hi = readBE16(f);
    uint32_t lo = readBE16(f);
    return (hi << 16) | lo;
}

int32_t readLE32(File& f) {
    uint8_t b[4] = {0, 0, 0, 0};
    if (f.read(b, 4) != 4) return 0;
    return static_cast<int32_t>(
        static_cast<uint32_t>(b[0]) |
        (static_cast<uint32_t>(b[1]) << 8) |
        (static_cast<uint32_t>(b[2]) << 16) |
        (static_cast<uint32_t>(b[3]) << 24));
}

void scanDirectoryForImages(File dir, std::vector<String>& outPaths, size_t maxFiles) {
    if (!dir || !dir.isDirectory()) return;
    File entry = dir.openNextFile();
    while (entry && outPaths.size() < maxFiles) {
        if (!entry.isDirectory()) {
            String path = String(entry.path());
            if (!path.startsWith("/")) {
                path = "/" + path;
            }
            if (!isHiddenOrMacMetadata(path) && hasSupportedExtension(path)) {
                if (std::find(outPaths.begin(), outPaths.end(), path) == outPaths.end()) {
                    outPaths.push_back(path);
                }
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
}

} // namespace

bool BackgroundManager::begin() {
    _fsReady = LittleFS.begin(true);
    return _fsReady;
}

bool BackgroundManager::hasCustomBackground() const {
    if (!_fsReady) return false;
    if (!LittleFS.exists(Config::BG_PATH_LANDSCAPE) || !LittleFS.exists(Config::BG_PATH_PORTRAIT)) {
        return false;
    }
    File fl = LittleFS.open(Config::BG_PATH_LANDSCAPE, "r");
    File fp = LittleFS.open(Config::BG_PATH_PORTRAIT, "r");
    bool valid = fl && fp && (fl.size() == EXPECTED_BG_BYTES) && (fp.size() == EXPECTED_BG_BYTES);
    if (fl) fl.close();
    if (fp) fp.close();
    return valid;
}

String BackgroundManager::lastImportedName() const {
    if (!_fsReady || !LittleFS.exists(Config::BG_META_PATH)) return "";
    File f = LittleFS.open(Config::BG_META_PATH, "r");
    if (!f) return "";
    String line = f.readStringUntil('\n');
    f.close();
    line.trim();
    int sep = line.indexOf('|');
    return (sep > 0) ? line.substring(0, sep) : line;
}

void BackgroundManager::drawFull(CydDisplay& lcd) {
    drawRect(lcd, 0, 0, lcd.width(), lcd.height());
}

void BackgroundManager::drawRect(CydDisplay& lcd, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    const int16_t fullW = lcd.width();
    const int16_t fullH = lcd.height();

    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > fullW) rw = fullW - rx;
    if (ry + rh > fullH) rh = fullH - ry;
    if (rw <= 0 || rh <= 0) return;

    const bool isLand = (fullW >= fullH);
    const char* bgPath = isLand ? Config::BG_PATH_LANDSCAPE : Config::BG_PATH_PORTRAIT;

    if (_fsReady && LittleFS.exists(bgPath)) {
        File f = LittleFS.open(bgPath, "r");
        if (f && f.size() == static_cast<size_t>(fullW) * static_cast<size_t>(fullH) * 2UL) {
            // Stream directly to ST7789 using a compact stack/static buffer
            constexpr int CHUNK_ROWS = 16;
            static uint16_t lineBuf[320 * CHUNK_ROWS];

            lcd.startWrite();
            if (rx == 0 && rw == fullW) {
                // Fast contiguous read for full-width regions
                f.seek(static_cast<size_t>(ry) * static_cast<size_t>(fullW) * 2UL);
                int16_t y = ry;
                int16_t remaining = rh;
                while (remaining > 0) {
                    int16_t rows = (remaining > CHUNK_ROWS) ? CHUNK_ROWS : remaining;
                    size_t bytesToRead = static_cast<size_t>(fullW) * static_cast<size_t>(rows) * 2UL;
                    if (f.read(reinterpret_cast<uint8_t*>(lineBuf), bytesToRead) != bytesToRead) break;
                    // Mirror rows/cols automatically if user chose a flipped landscape/portrait rotation?
                    // Note: Both landscape rotations are 320x240 and LovyanGFX handles screen coordinates!
                    lcd.pushImage(0, y, fullW, rows, reinterpret_cast<const lgfx::swap565_t*>(lineBuf));
                    y += rows;
                    remaining -= rows;
                }
            } else {
                // Sub-rectangle restore (e.g. hiding status pill without redrawing entire screen)
                for (int16_t row = 0; row < rh; ++row) {
                    size_t offset = (static_cast<size_t>(ry + row) * static_cast<size_t>(fullW) +
                                     static_cast<size_t>(rx)) * 2UL;
                    f.seek(offset);
                    size_t rowBytes = static_cast<size_t>(rw) * 2UL;
                    if (f.read(reinterpret_cast<uint8_t*>(lineBuf), rowBytes) != rowBytes) break;
                    lcd.pushImage(rx, ry + row, rw, 1, reinterpret_cast<const lgfx::swap565_t*>(lineBuf));
                }
            }
            lcd.endWrite();
            f.close();
            return;
        }
        if (f) f.close();
    }

    drawProceduralRect(lcd, fullW, fullH, rx, ry, rw, rh);
}

void BackgroundManager::drawProceduralRect(CydDisplay& lcd, int16_t fullW, int16_t fullH,
                                           int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    (void)fullW;
    lcd.startWrite();
    for (int16_t y = ry; y < ry + rh; ++y) {
        // Subtle dark slate vertical gradient
        uint8_t shade = static_cast<uint8_t>(14 + (y * 14) / (fullH > 0 ? fullH : 240));
        uint16_t baseColor = lcd.color565(shade, shade + 4, shade + 12);
        lcd.drawFastHLine(rx, y, rw, baseColor);

        // Subtle dot-grid every 24 pixels
        if ((y % 24) == 12) {
            uint16_t dotColor = lcd.color565(shade + 22, shade + 28, shade + 40);
            int16_t firstX = rx + ((12 - (rx % 24) + 24) % 24);
            for (int16_t x = firstX; x < rx + rw; x += 24) {
                lcd.drawPixel(x, y, dotColor);
            }
        }
    }
    lcd.endWrite();
}

bool BackgroundManager::mountSd(String& outError) {
    if (_sdMounted) return true;

    pinMode(Config::PIN_SD_CS, OUTPUT);
    digitalWrite(Config::PIN_SD_CS, HIGH);
    _sdSpi.begin(Config::PIN_SD_SCLK, Config::PIN_SD_MISO, Config::PIN_SD_MOSI, Config::PIN_SD_CS);

    if (!SD.begin(Config::PIN_SD_CS, _sdSpi, Config::SD_SPI_FREQ_HZ)) {
        _sdSpi.end();
        outError = "No FAT32 microSD card detected";
        return false;
    }

    _sdMounted = true;
    return true;
}

void BackgroundManager::unmountSd() {
    if (_sdMounted) {
        SD.end();
        _sdSpi.end();
        _sdMounted = false;
    }
}

bool BackgroundManager::listSdImages(std::vector<String>& outPaths, String& outError) {
    outPaths.clear();
    if (!mountSd(outError)) return false;

    // Priority filenames checked first
    const char* priorityNames[] = {
        "/bg.jpg", "/bg.jpeg", "/bg.png", "/bg.bmp",
        "/background.jpg", "/background.jpeg", "/background.png", "/background.bmp"
    };
    for (const char* p : priorityNames) {
        if (SD.exists(p)) {
            outPaths.push_back(String(p));
        }
    }

    // Scan /backgrounds and / root
    if (SD.exists("/backgrounds")) {
        File bgDir = SD.open("/backgrounds");
        scanDirectoryForImages(bgDir, outPaths, 24);
        if (bgDir) bgDir.close();
    }

    File root = SD.open("/");
    scanDirectoryForImages(root, outPaths, 24);
    if (root) root.close();

    unmountSd();

    if (outPaths.empty()) {
        outError = "No .jpg, .png, or .bmp files found on SD";
        return false;
    }
    return true;
}

bool BackgroundManager::checkBootImportCandidate(String& outSdPath) {
    String err;
    if (!mountSd(err)) return false;

    const char* candidates[] = {
        "/bg.jpg", "/bg.jpeg", "/bg.png", "/bg.bmp",
        "/background.jpg", "/background.jpeg", "/background.png", "/background.bmp"
    };

    String foundPath = "";
    size_t foundSize = 0;

    for (const char* c : candidates) {
        if (SD.exists(c)) {
            File f = SD.open(c, "r");
            if (f && f.size() > 32) {
                foundPath = String(c);
                foundSize = f.size();
                f.close();
                break;
            }
            if (f) f.close();
        }
    }

    unmountSd();

    if (foundPath.isEmpty()) return false;

    // Check if this exact path + file size was already imported or dismissed
    String signature = foundPath + "|" + String(foundSize);
    if (_fsReady && LittleFS.exists(Config::BG_META_PATH)) {
        File meta = LittleFS.open(Config::BG_META_PATH, "r");
        if (meta) {
            String savedSig = meta.readStringUntil('\n');
            meta.close();
            savedSig.trim();
            if (savedSig == signature && hasCustomBackground()) {
                return false;
            }
        }
    }

    outSdPath = foundPath;
    return true;
}

void BackgroundManager::markCandidateHandled(const String& sdPath, size_t fileSize) {
    if (!_fsReady) return;
    File meta = LittleFS.open(Config::BG_META_PATH, "w");
    if (meta) {
        meta.println(sdPath + "|" + String(fileSize));
        meta.close();
    }
}

auto BackgroundManager::detectFormatAndSize(File& file, int32_t& outW, int32_t& outH) -> ImageFormat {
    outW = 0;
    outH = 0;
    if (!file || file.size() < 26) return ImageFormat::Unknown;

    file.seek(0);
    uint8_t sig[8] = {0};
    if (file.read(sig, 8) != 8) return ImageFormat::Unknown;

    // PNG signature: 89 50 4E 47 0D 0A 1A 0A
    if (sig[0] == 0x89 && sig[1] == 0x50 && sig[2] == 0x4E && sig[3] == 0x47) {
        file.seek(16);
        outW = static_cast<int32_t>(readBE32(file));
        outH = static_cast<int32_t>(readBE32(file));
        return (outW > 0 && outH > 0) ? ImageFormat::Png : ImageFormat::Unknown;
    }

    // BMP signature: 'B' 'M'
    if (sig[0] == 'B' && sig[1] == 'M') {
        file.seek(18);
        outW = readLE32(file);
        outH = abs(readLE32(file));
        return (outW > 0 && outH > 0) ? ImageFormat::Bmp : ImageFormat::Unknown;
    }

    // JPEG signature: FF D8
    if (sig[0] == 0xFF && sig[1] == 0xD8) {
        file.seek(2);
        while (file.available()) {
            int c = file.read();
            if (c != 0xFF) continue;
            while (c == 0xFF && file.available()) {
                c = file.read();
            }
            if (c < 0) break;

            // Standalone markers without length
            if (c == 0xD8 || c == 0xD9 || (c >= 0xD0 && c <= 0xD7) || c == 0x01) {
                continue;
            }

            uint16_t segLen = readBE16(file);
            if (segLen < 2) break;

            // SOF0..SOF15 markers (excluding DHT=C4, JPG=C8, DAC=CC)
            if ((c >= 0xC0 && c <= 0xCF) && c != 0xC4 && c != 0xC8 && c != 0xCC) {
                file.read(); // sample precision
                outH = static_cast<int32_t>(readBE16(file));
                outW = static_cast<int32_t>(readBE16(file));
                return (outW > 0 && outH > 0) ? ImageFormat::Jpeg : ImageFormat::Unknown;
            }

            size_t nextPos = file.position() + segLen - 2;
            if (nextPos >= file.size()) break;
            file.seek(nextPos);
        }
    }

    return ImageFormat::Unknown;
}

bool BackgroundManager::renderTargetToFlash(File& sdFile, ImageFormat fmt, int32_t imgW, int32_t imgH,
                                            int16_t targetW, int16_t targetH, const char* destPath,
                                            int baseProgress, int spanProgress,
                                            ProgressCallback& onProgress, String& outError) {
    // Compute uniform aspect-fill scale & centered offset
    const float scaleX = static_cast<float>(targetW) / static_cast<float>(imgW);
    const float scaleY = static_cast<float>(targetH) / static_cast<float>(imgH);
    const float scale  = (scaleX > scaleY) ? scaleX : scaleY;

    const int32_t scaledW = static_cast<int32_t>(roundf(static_cast<float>(imgW) * scale));
    const int32_t scaledH = static_cast<int32_t>(roundf(static_cast<float>(imgH) * scale));
    const int32_t drawX   = (targetW - scaledW) / 2;
    const int32_t drawY   = (targetH - scaledH) / 2;

    // 40-row strip uses <= 25.6 KB RAM, completing 240/320px height in 6-8 fast passes
    constexpr int16_t STRIP_H = 40;
    lgfx::LGFX_Sprite strip;
    strip.setColorDepth(lgfx::rgb565_2Byte);
    if (!strip.createSprite(targetW, STRIP_H)) {
        outError = "Out of memory creating render buffer";
        return false;
    }

    if (LittleFS.exists(Config::BG_PATH_TEMP)) {
        LittleFS.remove(Config::BG_PATH_TEMP);
    }
    File out = LittleFS.open(Config::BG_PATH_TEMP, "w");
    if (!out) {
        strip.deleteSprite();
        outError = "Failed to open LittleFS for writing";
        return false;
    }

    for (int16_t stripY = 0; stripY < targetH; stripY += STRIP_H) {
        int16_t curH = (stripY + STRIP_H <= targetH) ? STRIP_H : (targetH - stripY);
        strip.fillSprite(TFT_BLACK);

        sdFile.seek(0);
        bool decoded = false;
        switch (fmt) {
            case ImageFormat::Jpeg:
                decoded = strip.drawJpg(&sdFile, drawX, drawY - stripY, 0, 0, 0, 0, scale, scale);
                break;
            case ImageFormat::Png:
                decoded = strip.drawPng(&sdFile, drawX, drawY - stripY, 0, 0, 0, 0, scale, scale);
                break;
            case ImageFormat::Bmp:
                decoded = strip.drawBmp(&sdFile, drawX, drawY - stripY, 0, 0, 0, 0, scale, scale);
                break;
            default:
                break;
        }

        if (!decoded) {
            out.close();
            LittleFS.remove(Config::BG_PATH_TEMP);
            strip.deleteSprite();
            outError = "Image decode failed (check format/compression)";
            return false;
        }

        size_t bytesToWrite = static_cast<size_t>(targetW) * static_cast<size_t>(curH) * 2UL;
        const uint8_t* rawBuf = static_cast<const uint8_t*>(strip.getBuffer());
        if (out.write(rawBuf, bytesToWrite) != bytesToWrite) {
            out.close();
            LittleFS.remove(Config::BG_PATH_TEMP);
            strip.deleteSprite();
            outError = "Flash write error (LittleFS full?)";
            return false;
        }

        if (onProgress) {
            int pct = baseProgress + ((stripY + curH) * spanProgress) / targetH;
            onProgress(pct, (targetW > targetH) ? "Saving Landscape..." : "Saving Portrait...");
        }
        yield();
    }

    out.close();
    strip.deleteSprite();

    if (LittleFS.exists(destPath)) {
        LittleFS.remove(destPath);
    }
    if (!LittleFS.rename(Config::BG_PATH_TEMP, destPath)) {
        outError = "Failed to finalize background file in flash";
        return false;
    }

    return true;
}

bool BackgroundManager::importFromSd(const String& sdPath, ProgressCallback onProgress, String& outError) {
    if (!_fsReady) {
        outError = "Internal LittleFS flash not mounted";
        return false;
    }
    if (!mountSd(outError)) {
        return false;
    }

    if (onProgress) onProgress(2, "Reading SD image...");

    File sdFile = SD.open(sdPath, "r");
    if (!sdFile) {
        unmountSd();
        outError = "Cannot open " + sdPath;
        return false;
    }

    const size_t fileSize = sdFile.size();
    int32_t imgW = 0, imgH = 0;
    ImageFormat fmt = detectFormatAndSize(sdFile, imgW, imgH);
    if (fmt == ImageFormat::Unknown || imgW <= 0 || imgH <= 0) {
        sdFile.close();
        unmountSd();
        outError = "Unsupported or corrupt image header";
        return false;
    }

    // Guard against huge PNGs exhausting non-PSRAM ESP32 heap during inflate
    if (fmt == ImageFormat::Png && (imgW > 1024 || imgH > 1024)) {
        sdFile.close();
        unmountSd();
        outError = "PNG > 1024px; use JPG or BMP for high-res photos";
        return false;
    }

    if (onProgress) onProgress(5, "Converting Landscape...");
    if (!renderTargetToFlash(sdFile, fmt, imgW, imgH, 320, 240,
                             Config::BG_PATH_LANDSCAPE, 5, 45, onProgress, outError)) {
        sdFile.close();
        unmountSd();
        return false;
    }

    if (onProgress) onProgress(52, "Converting Portrait...");
    if (!renderTargetToFlash(sdFile, fmt, imgW, imgH, 240, 320,
                             Config::BG_PATH_PORTRAIT, 52, 46, onProgress, outError)) {
        sdFile.close();
        unmountSd();
        return false;
    }

    sdFile.close();
    unmountSd();

    markCandidateHandled(sdPath, fileSize);
    if (onProgress) onProgress(100, "Saved to Flash! Remove SD card.");
    return true;
}

void BackgroundManager::clearCustomBackground() {
    if (!_fsReady) return;
    if (LittleFS.exists(Config::BG_PATH_LANDSCAPE)) LittleFS.remove(Config::BG_PATH_LANDSCAPE);
    if (LittleFS.exists(Config::BG_PATH_PORTRAIT))  LittleFS.remove(Config::BG_PATH_PORTRAIT);
    if (LittleFS.exists(Config::BG_META_PATH))      LittleFS.remove(Config::BG_META_PATH);
}
