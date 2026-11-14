#pragma once

#include <Arduino.h>
#include "BleHidMouse.h"
#include "SettingsManager.h"

enum class GestureEvent : uint8_t {
    None              = 0,
    LeftClick         = 1,
    DoubleClick       = 2,
    RightClick        = 3,
    DragStart         = 4,
    DragEnd           = 5,
    Scrolling         = 6,
    ShortcutTriggered = 7,
    OpenSettings      = 8
};

class TouchpadEngine {
public:
    void begin(BleHidMouse* mouse, SettingsManager* settings);
    void reset();

    /**
     * Call at regular intervals (~125 Hz / every 8 ms) when in Touchpad mode.
     * @param rawTouched True if XPT2046 currently detects a touch
     * @param rawX       Calibrated screen X coordinate (may extend slightly outside 0..screenW-1 near bezel)
     * @param rawY       Calibrated screen Y coordinate (may extend slightly outside 0..screenH-1 near bezel)
     * @param screenW    Current display width in px
     * @param screenH    Current display height in px
     * @return Any discrete gesture event triggered this tick (for optional HUD feedback)
     */
    GestureEvent update(bool rawTouched, int16_t rawX, int16_t rawY, int16_t screenW, int16_t screenH);

    bool isDragging() const { return _state == State::Dragging; }
    ShortcutAction lastShortcutAction() const { return _lastShortcut; }
    int16_t lastX() const { return static_cast<int16_t>(_filtX); }
    int16_t lastY() const { return static_cast<int16_t>(_filtY); }

private:
    enum class State : uint8_t {
        Idle,
        Touching,
        Moving,
        PendingTapRelease,
        SecondTouch,
        Dragging,
        ActionCorner,
        BottomStripPending,
        BottomSwipeUp,
        ScrollCorner,
        VScroll,
        HScroll,
        CornerRightTap,
        ConsumedHold
    };

    void accumulateRelativeMove(float rawDx, float rawDy, uint32_t dtMs);
    void flushRelativeMove(bool isDrag, uint32_t now);
    GestureEvent emitScrollIfReady(uint32_t now, bool vertical, bool naturalScroll);
    GestureEvent emitHorizontalSwipeStep(bool naturalScroll);
    GestureEvent triggerShortcutAction(ShortcutAction action);
    GestureEvent resolveCornerTapCount(uint8_t tapCount, const TouchpadSettings& cfg);

    BleHidMouse*     _mouse              = nullptr;
    SettingsManager* _settings           = nullptr;

    State    _state                      = State::Idle;
    int      _settleCount                = 0;
    int      _releaseCount               = 0;
    int      _pendingMakeBounce          = 0;
    uint16_t _touchTicks                 = 0;
    uint16_t _reqTapTicks                = 3;
    bool     _tapEligible                = true;

    // 3-sample median ring buffer + EMA filter + spike recovery
    int16_t  _histX[3]                   = {0, 0, 0};
    int16_t  _histY[3]                   = {0, 0, 0};
    uint8_t  _histIdx                    = 0;
    uint8_t  _histFilled                 = 0;
    int16_t  _lastRawX                   = 0;
    int16_t  _lastRawY                   = 0;
    bool     _jumpPending                = false;
    int16_t  _jumpX                      = 0;
    int16_t  _jumpY                      = 0;
    float    _filtX                      = 0.0f;
    float    _filtY                      = 0.0f;
    float    _prevFiltX                  = 0.0f;
    float    _prevFiltY                  = 0.0f;

    // Gesture anchors & accumulators
    int16_t  _entryX                     = 0;
    int16_t  _entryY                     = 0;
    float    _startX                     = 0.0f;
    float    _startY                     = 0.0f;
    float    _minSwipeY                  = 0.0f;
    uint32_t _firstTouchMs               = 0;
    uint32_t _touchDownMs                = 0;
    uint32_t _tapReleaseMs               = 0;
    uint32_t _lastMotionEndMs            = 0;
    uint32_t _lastMoveSampleMs           = 0;
    uint32_t _lastDragReportMs           = 0;
    uint32_t _lastScrollReportMs         = 0;
    uint32_t _dragPressMs                = 0;

    float    _remX                       = 0.0f;
    float    _remY                       = 0.0f;
    float    _scrollAccum                = 0.0f;

    // Discrete 1-page-per-swipe horizontal scroll state
    int8_t   _hScrollFiredDir            = 0;
    float    _hScrollAnchorX             = 0.0f;
    int8_t   _lastHScrollDir             = 0;
    float    _lastHScrollEndX            = 0.0f;
    uint32_t _lastHScrollEndMs           = 0;

    // Corner multi-tap & bottom swipe-up state
    uint8_t        _cornerTapCount       = 0;
    uint32_t       _cornerLastReleaseMs  = 0;
    float          _swipeHoldAnchorX     = 0.0f;
    float          _swipeHoldAnchorY     = 0.0f;
    uint32_t       _swipeHoldStartMs     = 0;
    ShortcutAction _lastShortcut         = ShortcutAction::None;
};
