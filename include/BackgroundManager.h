#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPI.h>
#include <functional>
#include <vector>
#include "CydDisplay.h"

class BackgroundManager {
public:
    using ProgressCallback = std::function<void(int percent, const char* stage)>;

    bool begin();

    bool hasCustomBackground() const;
    String lastImportedName() const;

    // Fast full-screen or dirty-rectangle restore from LittleFS (or procedural fallback)
    void drawFull(CydDisplay& lcd);
    void drawRect(CydDisplay& lcd, int16_t rx, int16_t ry, int16_t rw, int16_t rh);

    // SD Card operations (uses dedicated Hardware VSPI on pins 18/19/23/5)
    bool listSdImages(std::vector<String>& outPaths, String& outError);
    bool checkBootImportCandidate(String& outSdPath);
    void markCandidateHandled(const String& sdPath, size_t fileSize);

    bool importFromSd(const String& sdPath, ProgressCallback onProgress, String& outError);
    void clearCustomBackground();

private:
    enum class ImageFormat : uint8_t {
        Unknown,
        Jpeg,
        Png,
        Bmp
    };

    bool mountSd(String& outError);
    void unmountSd();

    static ImageFormat detectFormatAndSize(File& file, int32_t& outW, int32_t& outH);
    bool renderTargetToFlash(File& sdFile, ImageFormat fmt, int32_t imgW, int32_t imgH,
                             int16_t targetW, int16_t targetH, const char* destPath,
                             int baseProgress, int spanProgress,
                             ProgressCallback& onProgress, String& outError);

    void drawProceduralRect(CydDisplay& lcd, int16_t fullW, int16_t fullH,
                            int16_t rx, int16_t ry, int16_t rw, int16_t rh);

    SPIClass _sdSpi{VSPI};
    bool     _fsReady = false;
    bool     _sdMounted = false;
};
