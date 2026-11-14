#include "TouchpadEngine.h"
#include <math.h>
#include "Config.h"

namespace {

int16_t median3(int16_t a, int16_t b, int16_t c) {
    if (a > b) { int16_t t = a; a = b; b = t; }
    if (b > c) { b = c; }
    return (a > b) ? a : b;
}

int16_t clampCoord(int16_t val, int16_t lo, int16_t hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

constexpr int      TAP_RELEASE_DEBOUNCE_TICKS    = 2;     // 16 ms for crisp tap release
constexpr int      MOTION_RELEASE_DEBOUNCE_TICKS = 3;     // 24 ms bridges 1-2 tick glide dropouts without eating quick taps
constexpr int      SCROLL_RELEASE_DEBOUNCE_TICKS = 4;     // 32 ms bridges edge-strip pressure dips
constexpr int      HSCROLL_RELEASE_DEBOUNCE_TICKS = 7;    // 56 ms bridges pressure dips during slow horizontal swipes
constexpr int      DRAG_RELEASE_DEBOUNCE_TICKS   = 4;     // 32 ms bridges dropouts while preserving swipe flick momentum
constexpr int      MAKE_BOUNCE_TOLERANCE_TICKS   = 2;     // Bridges initial contact chatter when starting touch #2
constexpr int      MIN_TAP_TICKS                 = 3;     // >= 24 ms contact (2 settle + 1 anchor) rejects glitches
constexpr int      FAST_FOLLOW_TAP_TICKS         = 4;     // >= 32 ms required when tapping 45..130ms right after a swipe
constexpr uint32_t FAST_FOLLOW_WINDOW_MS         = 130;
constexpr float    DRAG_START_PX                 = 7.0f;  // ~1.1mm slide engages LEFT button only once finger is moving
constexpr float    DRAG_GAIN_BOOST               = 1.55f; // Lets a 1-stroke swipe across 204px pad span full iPhone width
constexpr uint32_t DRAG_PRESS_SETTLE_MS          = 22;    // Separate BLE event so iOS AssistiveTouch creates UITouchBegan cleanly
constexpr uint32_t DRAG_REPORT_INTERVAL_MS       = 16;    // ~60 Hz uniform drag pacing for iOS AssistiveTouch
constexpr int16_t  MAX_DRAG_STEP_DELTA           = 36;    // Uniform steps keep iOS AssistiveTouch gesture tracking smooth
constexpr float    MAX_SINGLE_TICK_JUMP_PX       = 65.0f; // Rejects 1-tick resistive peel-off spikes (re-syncs if sustained)
constexpr int16_t  SCROLL_HIT_SLACK_PX           = 6;     // Extra inward hit margin so fingers on the divider line scroll
constexpr int16_t  ACTION_CORNER_SLACK_PX        = 10;    // Generous ~46x44px hit box around bottom-right Home icon
constexpr int16_t  BOTTOM_BAND_SLACK_PX          = 10;    // ~44px bottom entry zone so thumb swipes catch reliably
constexpr float    CORNER_SLIDE_OUT_PX           = 22.0f; // Prevents resistive bezel pressure shift from aborting taps
constexpr float    SCROLL_PIXELS_PER_STEP        = 6.5f;  // ~1.15mm finger travel per vertical scroll step
constexpr uint32_t SCROLL_REPORT_INTERVAL_MS     = 24;    // Paced to BLE connection interval so iOS doesn't drop wheel
constexpr int      SCROLL_UNITS_PER_STEP         = 4;     // Multi-unit step gives responsive iOS AssistiveTouch scrolling
constexpr float    HSCROLL_SWIPE_TRIGGER_PX      = 20.0f; // ~3.5mm horizontal travel fires 1 discrete page step per swipe
constexpr int8_t   HSCROLL_PAGE_UNITS            = 4;     // Single AC Pan pulse magnitude to advance 1 iOS Home Screen
constexpr uint32_t HSCROLL_DROPOUT_BRIDGE_MS     = 120;   // Prevents mid-stroke resistive dropouts from double-stepping
constexpr uint32_t CORNER_MULTITAP_MS            = 380;   // Multi-tap window for 1x / 2x / 3x corner clicks
constexpr float    BOTTOM_SWIPE_ENGAGE_PX        = 10.0f; // Upward travel out of bottom bar to enter swipe-up
constexpr float    BOTTOM_SWIPE_FIRE_PX          = 22.0f; // Peak upward travel required to fire Home on release
constexpr uint32_t BOTTOM_SWIPE_HOLD_MS          = 350;   // Pause duration after swiping up to open App Switcher

uint8_t maxConfiguredCornerTaps(const TouchpadSettings& cfg) {
    if (cfg.tripleTapAction != ShortcutAction::None) return 3;
    if (cfg.doubleTapAction != ShortcutAction::None) return 2;
    return 1;
}

} // namespace

void TouchpadEngine::begin(BleHidMouse* mouse, SettingsManager* settings) {
    _mouse    = mouse;
    _settings = settings;
    reset();
}

void TouchpadEngine::reset() {
    if (_mouse && _state == State::Dragging) {
        _mouse->release(MOUSE_BTN_LEFT);
    }
    _state              = State::Idle;
    _settleCount        = 0;
    _releaseCount       = 0;
    _pendingMakeBounce  = 0;
    _touchTicks         = 0;
    _reqTapTicks        = MIN_TAP_TICKS;
    _tapEligible        = true;
    _histFilled         = 0;
    _jumpPending        = false;
    _firstTouchMs       = 0;
    _lastDragReportMs   = 0;
    _lastScrollReportMs = 0;
    _dragPressMs        = 0;
    _remX                = 0.0f;
    _remY                = 0.0f;
    _scrollAccum         = 0.0f;
    _hScrollFiredDir     = 0;
    _hScrollAnchorX      = 0.0f;
    _lastHScrollDir      = 0;
    _lastHScrollEndX     = 0.0f;
    _lastHScrollEndMs    = 0;
    _entryX              = 0;
    _entryY              = 0;
    _minSwipeY           = 0.0f;
    _cornerTapCount      = 0;
    _cornerLastReleaseMs = 0;
    _swipeHoldAnchorX    = 0.0f;
    _swipeHoldAnchorY    = 0.0f;
    _swipeHoldStartMs    = 0;
    _lastShortcut        = ShortcutAction::None;
}

GestureEvent TouchpadEngine::update(bool rawTouched, int16_t rawX, int16_t rawY,
                                    int16_t screenW, int16_t screenH) {
    if (!_mouse || !_settings) return GestureEvent::None;

    const uint32_t now = millis();
    const TouchpadSettings& cfg = _settings->get();
    const uint32_t longClickMs  = _settings->getLongClickMs();

    // Check pending multi-tap timeout on the bottom-right Action Corner while finger is lifted
    if (!rawTouched && _cornerTapCount > 0 && _state == State::Idle) {
        if (_settleCount > 0 && _pendingMakeBounce < (MAKE_BOUNCE_TOLERANCE_TICKS + 1)) {
            _pendingMakeBounce++;
            return GestureEvent::None;
        }
        _settleCount       = 0;
        _pendingMakeBounce = 0;
        _firstTouchMs      = 0;
        _touchTicks        = 0;
        _jumpPending       = false;
        if (now - _cornerLastReleaseMs >= CORNER_MULTITAP_MS) {
            const uint8_t taps = _cornerTapCount;
            _cornerTapCount    = 0;
            Serial.printf("[TP] ActionCorner resolved %ux tap\n", taps);
            return resolveCornerTapCount(taps, cfg);
        }
        return GestureEvent::None;
    }

    // Check delayed single-tap timeout while waiting for a potential second touch (touch-release-touch-drag)
    if (!rawTouched && _state == State::PendingTapRelease) {
        // If touch #2 already began making contact, tolerate 1-2 ticks of resistive make-contact chatter
        if (_settleCount > 0 && _pendingMakeBounce < MAKE_BOUNCE_TOLERANCE_TICKS) {
            _pendingMakeBounce++;
            return GestureEvent::None;
        }

        _settleCount       = 0;
        _pendingMakeBounce = 0;
        _firstTouchMs      = 0;
        _touchTicks        = 0;
        _jumpPending       = false;
        if (now - _tapReleaseMs >= Config::PENDING_TAP_MS) {
            Serial.printf("[TP] SingleClick (air=%lu ms)\n", static_cast<unsigned long>(now - _tapReleaseMs));
            _mouse->click(MOUSE_BTN_LEFT);
            _state = State::Idle;
            return GestureEvent::LeftClick;
        }
        return GestureEvent::None;
    }

    // Handle touch release (state-aware debounce bridges resistive dropouts cleanly)
    if (!rawTouched) {
        if (_state == State::Idle) {
            _settleCount       = 0;
            _pendingMakeBounce = 0;
            _firstTouchMs      = 0;
            _touchTicks        = 0;
            _jumpPending       = false;
            return GestureEvent::None;
        }

        int debounceTicks = TAP_RELEASE_DEBOUNCE_TICKS;
        if (_state == State::Dragging) {
            debounceTicks = DRAG_RELEASE_DEBOUNCE_TICKS;
            // Continue draining any queued drag movement smoothly before button release
            if (now - _dragPressMs >= DRAG_PRESS_SETTLE_MS) {
                flushRelativeMove(true, now);
            }
        } else if (_state == State::HScroll) {
            debounceTicks = HSCROLL_RELEASE_DEBOUNCE_TICKS;
        } else if (_state == State::VScroll ||
                   _state == State::ScrollCorner) {
            debounceTicks = SCROLL_RELEASE_DEBOUNCE_TICKS;
        } else if (_state == State::Moving ||
                   _state == State::BottomStripPending ||
                   _state == State::BottomSwipeUp) {
            debounceTicks = MOTION_RELEASE_DEBOUNCE_TICKS;
        }

        _releaseCount++;
        if (_releaseCount < debounceTicks) {
            return GestureEvent::None;
        }

        // Estimate true finger-lift timestamp prior to debounce frames
        const uint32_t liftSubMs = static_cast<uint32_t>((debounceTicks - 1) * 8);
        const uint32_t releaseMs = (now > liftSubMs) ? (now - liftSubMs) : now;
        const uint32_t heldMs    = (releaseMs > _touchDownMs) ? (releaseMs - _touchDownMs) : 0;
        const uint16_t ticksHeld = _touchTicks;

        // Confirmed finger lift
        _settleCount       = 0;
        _releaseCount      = 0;
        _pendingMakeBounce = 0;
        _histFilled        = 0;
        _jumpPending       = false;
        _firstTouchMs      = 0;
        _touchTicks        = 0;

        GestureEvent evt = GestureEvent::None;

        switch (_state) {
            case State::Touching:
                if (_tapEligible &&
                    ticksHeld >= _reqTapTicks &&
                    heldMs >= Config::TAP_MIN_MS &&
                    heldMs <= Config::TAP_MAX_MS) {
                    _state        = State::PendingTapRelease;
                    _tapReleaseMs = releaseMs;
                    Serial.printf("[TP] Tap1 released (held=%lu ms) -> awaiting Touch2\n",
                                  static_cast<unsigned long>(heldMs));
                } else {
                    Serial.printf("[TP] Touch1 ignored (elig=%d ticks=%u held=%lu ms)\n",
                                  _tapEligible ? 1 : 0, ticksHeld, static_cast<unsigned long>(heldMs));
                    _state = State::Idle;
                }
                break;

            case State::SecondTouch: {
                if (ticksHeld >= MIN_TAP_TICKS &&
                    heldMs >= Config::TAP_MIN_MS &&
                    heldMs <= Config::TAP_MAX_MS) {
                    // Quick second tap without dragging -> emit crisp Double Click
                    _mouse->click(MOUSE_BTN_LEFT);
                    delay(40);
                    _mouse->click(MOUSE_BTN_LEFT);
                    Serial.println("[TP] DoubleClick");
                    evt = GestureEvent::DoubleClick;
                } else if (heldMs <= longClickMs) {
                    // Brief contact bounce or aborted second touch -> flush pending first tap
                    _mouse->click(MOUSE_BTN_LEFT);
                    Serial.println("[TP] SingleClick (flushed after Touch2)");
                    evt = GestureEvent::LeftClick;
                }
                _state = State::Idle;
                break;
            }

            case State::Moving:
            case State::ScrollCorner:
            case State::VScroll:
                _lastMotionEndMs = releaseMs;
                _state           = State::Idle;
                break;

            case State::HScroll:
                _lastMotionEndMs  = releaseMs;
                _lastHScrollDir   = _hScrollFiredDir;
                _lastHScrollEndX  = _filtX;
                _lastHScrollEndMs = releaseMs;
                _hScrollFiredDir  = 0;
                _state            = State::Idle;
                break;

            case State::BottomStripPending:
            case State::BottomSwipeUp: {
                const bool wasSwipeState = (_state == State::BottomSwipeUp);
                const float peakUpPx     = _startY - _minSwipeY;
                const float netHorizPx   = fabsf(_filtX - _startX);
                _lastMotionEndMs         = releaseMs;
                _state                   = State::Idle;
                if (peakUpPx >= BOTTOM_SWIPE_FIRE_PX &&
                    (wasSwipeState || peakUpPx > netHorizPx * 1.25f)) {
                    Serial.printf("[TP] BottomSwipeUp -> Home (peakUp=%.1f px)\n", peakUpPx);
                    evt = triggerShortcutAction(ShortcutAction::Home);
                }
                break;
            }

            case State::ActionCorner: {
                _state = State::Idle;
                if (ticksHeld >= 2 &&
                    heldMs >= 8 &&
                    heldMs < longClickMs) {
                    _cornerTapCount++;
                    _cornerLastReleaseMs = now;
                    const uint8_t maxTaps = maxConfiguredCornerTaps(cfg);
                    Serial.printf("[TP] ActionCorner tap #%u (max=%u, held=%lu ms)\n",
                                  _cornerTapCount, maxTaps, static_cast<unsigned long>(heldMs));
                    if (_cornerTapCount >= maxTaps) {
                        const uint8_t taps = _cornerTapCount;
                        _cornerTapCount    = 0;
                        evt = resolveCornerTapCount(taps, cfg);
                    }
                }
                break;
            }

            case State::Dragging:
                _mouse->release(MOUSE_BTN_LEFT);
                _lastMotionEndMs = releaseMs;
                _remX            = 0.0f;
                _remY            = 0.0f;
                _state           = State::Idle;
                Serial.println("[TP] DragEnd -> Released LEFT");
                evt              = GestureEvent::DragEnd;
                break;

            case State::CornerRightTap:
                if (_tapEligible &&
                    ticksHeld >= MIN_TAP_TICKS &&
                    heldMs >= Config::TAP_MIN_MS &&
                    heldMs <= longClickMs) {
                    _mouse->click(MOUSE_BTN_RIGHT);
                    evt = GestureEvent::RightClick;
                }
                _state = State::Idle;
                break;

            default:
                _state = State::Idle;
                break;
        }

        return evt;
    }

    // Soft-clamp near-bezel coordinates so slight calibration overhang still tracks smoothly
    rawX = clampCoord(rawX, -24, static_cast<int16_t>(screenW + 24));
    rawY = clampCoord(rawY, -24, static_cast<int16_t>(screenH + 24));

    // Finger is physically down
    if (_firstTouchMs == 0) {
        _firstTouchMs = now;
    }
    _touchTicks++;
    _releaseCount      = 0;
    _pendingMakeBounce = 0;

    // Discard initial settling frames on fresh resistive contact, while capturing entry point
    // so fast bottom-edge upward flicks and quick bezel-corner taps are never missed
    const bool cornerBtnActive = (cfg.homeGestureMode == HomeGestureMode::Both ||
                                  cfg.homeGestureMode == HomeGestureMode::CornerOnly);
    const bool swipeUpActive   = (cfg.homeGestureMode == HomeGestureMode::Both ||
                                  cfg.homeGestureMode == HomeGestureMode::SwipeOnly);
    const int16_t cornerHitW   = Config::VSCROLL_STRIP_WIDTH + ACTION_CORNER_SLACK_PX;
    const int16_t cornerHitH   = Config::HSCROLL_STRIP_HEIGHT + ACTION_CORNER_SLACK_PX;
    const int16_t bottomBandH  = Config::HSCROLL_STRIP_HEIGHT +
                                 (swipeUpActive ? BOTTOM_BAND_SLACK_PX : SCROLL_HIT_SLACK_PX);

    if (_settleCount == 0) {
        _entryX      = rawX;
        _entryY      = rawY;
        _settleCount = 1;
        return GestureEvent::None;
    }

    if (_settleCount < Config::SETTLE_SAMPLES) {
        const bool coherentEntry = (abs(rawX - _entryX) <= 45) && (abs(rawY - _entryY) <= 75);
        const bool earlyActionCorner = cornerBtnActive && coherentEntry &&
                                       (_entryX >= (screenW - cornerHitW)) &&
                                       (rawX >= (screenW - cornerHitW - 6)) &&
                                       (_entryY >= (screenH - cornerHitH)) &&
                                       (rawY >= (screenH - cornerHitH - 6));
        const bool earlyBottomSwipe  = !earlyActionCorner && swipeUpActive && coherentEntry &&
                                       (_entryY >= (screenH - bottomBandH)) &&
                                       (rawY <= (_entryY + 14));
        if (!earlyActionCorner && !earlyBottomSwipe) {
            _settleCount++;
            return GestureEvent::None;
        }
        _settleCount = Config::SETTLE_SAMPLES;
    }

    // Fresh contact transition
    if (_state == State::Idle || _state == State::PendingTapRelease) {
        const bool wasPendingTap = (_state == State::PendingTapRelease);

        _histX[0] = _histX[1] = _histX[2] = rawX;
        _histY[0] = _histY[1] = _histY[2] = rawY;
        _histIdx            = 0;
        _histFilled         = 3;
        _lastRawX           = rawX;
        _lastRawY           = rawY;
        _jumpPending        = false;
        _filtX              = static_cast<float>(rawX);
        _filtY              = static_cast<float>(rawY);
        _prevFiltX          = _filtX;
        _prevFiltY          = _filtY;
        _startX             = _filtX;
        _startY             = _filtY;
        _minSwipeY          = _filtY;
        _touchDownMs        = _firstTouchMs;
        _lastMoveSampleMs   = now;
        _lastDragReportMs   = 0;
        _lastScrollReportMs = 0;
        _dragPressMs        = 0;
        _remX               = 0.0f;
        _remY               = 0.0f;
        _scrollAccum        = 0.0f;
        _hScrollFiredDir    = 0;
        _hScrollAnchorX     = _startX;

        // Check elapsed time from true finger-lift of previous swipe (measured to start of this touch)
        const uint32_t sinceMotionMs = (_lastMotionEndMs == 0) ? 10000U
                                     : ((_firstTouchMs > _lastMotionEndMs) ? (_firstTouchMs - _lastMotionEndMs) : 0U);
        _tapEligible = (sinceMotionMs >= Config::POST_MOTION_TAP_SUPPRESS_MS);
        _reqTapTicks = (sinceMotionMs < FAST_FOLLOW_WINDOW_MS) ? FAST_FOLLOW_TAP_TICKS : MIN_TAP_TICKS;

        // Classify edge zones (with slight inward hit slack so fingers on the divider line catch cleanly)
        const bool hasRightStrip  = (cfg.scrollMode == ScrollMode::RightVertical ||
                                     cfg.scrollMode == ScrollMode::RightAndBottomBoth);
        const bool hasLeftStrip   = (cfg.scrollMode == ScrollMode::LeftVertical);
        const bool hasBottomStrip = (cfg.scrollMode == ScrollMode::RightAndBottomBoth);

        const int16_t vStripW = Config::VSCROLL_STRIP_WIDTH + SCROLL_HIT_SLACK_PX;
        const int16_t hStripH = Config::HSCROLL_STRIP_HEIGHT + SCROLL_HIT_SLACK_PX;

        const bool coherentEntry  = (abs(rawX - _entryX) <= 45) && (abs(rawY - _entryY) <= 85);
        const bool inActionCorner = cornerBtnActive &&
                                    ((rawX >= (screenW - cornerHitW) && rawY >= (screenH - cornerHitH)) ||
                                     (coherentEntry &&
                                      _entryX >= (screenW - cornerHitW) &&
                                      _entryY >= (screenH - cornerHitH) &&
                                      rawX >= (screenW - cornerHitW - 8) &&
                                      rawY >= (screenH - cornerHitH - 8)));

        bool inCornerRightClick = false;
        if (cfg.cornerRightClick && !inActionCorner) {
            bool inRightCorner = !hasRightStrip &&
                                 (rawX >= screenW - Config::CORNER_RCLICK_W) &&
                                 (rawY >= screenH - Config::CORNER_RCLICK_H);
            bool inLeftCorner  = (hasRightStrip || cornerBtnActive) &&
                                 (rawX < Config::CORNER_RCLICK_W) &&
                                 (rawY >= screenH - Config::CORNER_RCLICK_H);
            inCornerRightClick = (inRightCorner || inLeftCorner);
        }

        const bool inVStrip = !inActionCorner && !inCornerRightClick &&
                              ((hasRightStrip && rawX >= (screenW - vStripW)) ||
                               (hasLeftStrip && rawX < vStripW));
        const bool entryInBottomBand = coherentEntry &&
                                       (_entryY >= (screenH - bottomBandH)) &&
                                       !(cornerBtnActive && _entryX >= (screenW - cornerHitW));
        const bool inBottomBand = !inActionCorner && !inCornerRightClick &&
                                  ((rawY >= (screenH - bottomBandH)) || (swipeUpActive && entryInBottomBand));
        const bool inHStrip = !inActionCorner && !inCornerRightClick &&
                              hasBottomStrip && (rawY >= (screenH - hStripH));

        if (inBottomBand && swipeUpActive && entryInBottomBand && _entryY > rawY) {
            // Preserve upward distance already traveled during the first settle tick!
            _startY = static_cast<float>(_entryY);
        }

        if ((inHStrip || (inBottomBand && swipeUpActive)) &&
            _lastHScrollDir != 0 &&
            _lastHScrollEndMs != 0) {
            const uint32_t hAirGapMs = (_firstTouchMs > _lastHScrollEndMs) ? (_firstTouchMs - _lastHScrollEndMs) : 0U;
            if (hAirGapMs <= HSCROLL_DROPOUT_BRIDGE_MS && fabsf(_startX - _lastHScrollEndX) <= 28.0f) {
                // Bridge brief resistive dropout mid-swipe so a slow drag doesn't step a second page
                _hScrollFiredDir = _lastHScrollDir;
            }
        }

        GestureEvent flushedEvt = GestureEvent::None;
        if (_cornerTapCount > 0 && !inActionCorner) {
            const uint8_t taps = _cornerTapCount;
            _cornerTapCount    = 0;
            flushedEvt         = resolveCornerTapCount(taps, cfg);
        }

        if (wasPendingTap) {
            const uint32_t airGapMs = (_firstTouchMs > _tapReleaseMs) ? (_firstTouchMs - _tapReleaseMs) : 0U;
            if (!inActionCorner && !inVStrip && !inHStrip && !inCornerRightClick &&
                !(inBottomBand && swipeUpActive) &&
                (airGapMs <= Config::PENDING_TAP_MS)) {
                // Touch-release-touch in main pad: wait for actual slide before pressing LEFT
                // so stationary pause on Touch #2 never triggers an iOS long-press!
                _state = State::SecondTouch;
                Serial.printf("[TP] Touch2 (air=%lu ms) -> awaiting drag motion\n", static_cast<unsigned long>(airGapMs));
                return flushedEvt;
            }
            // User touched an edge/corner bar right after tapping -> flush single click immediately
            _mouse->click(MOUSE_BTN_LEFT);
            _tapEligible = false;
            flushedEvt   = GestureEvent::LeftClick;
        }

        if (inActionCorner) {
            _state = State::ActionCorner;
            return flushedEvt;
        }
        if (inCornerRightClick) {
            _state = State::CornerRightTap;
            return flushedEvt;
        }
        if (inVStrip && inHStrip) {
            _state = State::ScrollCorner;
            return flushedEvt;
        }
        if (inBottomBand && swipeUpActive) {
            _state = State::BottomStripPending;
            return flushedEvt;
        }
        if (inHStrip) {
            _state = State::HScroll;
            return flushedEvt;
        }
        if (inVStrip) {
            _state = State::VScroll;
            return flushedEvt;
        }

        _state = State::Touching;
        return flushedEvt;
    }

    const uint32_t dtMs = (now > _lastMoveSampleMs) ? (now - _lastMoveSampleMs) : 1;
    _lastMoveSampleMs = now;

    // Guard against isolated 1-tick resistive peel-off spikes, while allowing fast upward flicks
    const float maxJumpPx = (_state == State::BottomStripPending || _state == State::BottomSwipeUp)
                          ? 145.0f
                          : MAX_SINGLE_TICK_JUMP_PX;
    const float rawJump = hypotf(static_cast<float>(rawX - _lastRawX),
                                 static_cast<float>(rawY - _lastRawY));
    if (rawJump > maxJumpPx) {
        if (!_jumpPending) {
            _jumpPending = true;
            _jumpX       = rawX;
            _jumpY       = rawY;
            if (_state == State::Touching &&
                _tapEligible &&
                (now - _touchDownMs >= longClickMs)) {
                _mouse->click(MOUSE_BTN_RIGHT);
                _state = State::ConsumedHold;
                return GestureEvent::RightClick;
            }
            if (_state == State::ActionCorner &&
                _cornerTapCount == 0 &&
                (now - _touchDownMs >= longClickMs)) {
                _state = State::ConsumedHold;
                Serial.println("[TP] ActionCorner LongPress");
                return triggerShortcutAction(cfg.longPressAction);
            }
            return GestureEvent::None;
        }

        const float jumpAgree = hypotf(static_cast<float>(rawX - _jumpX),
                                       static_cast<float>(rawY - _jumpY));
        _jumpPending = false;
        if (jumpAgree <= maxJumpPx) {
            _lastRawX   = rawX;
            _lastRawY   = rawY;
            _histX[0]   = _histX[1] = _histX[2] = rawX;
            _histY[0]   = _histY[1] = _histY[2] = rawY;
            _histFilled = 3;
            _filtX      = static_cast<float>(rawX);
            _filtY      = static_cast<float>(rawY);
            _prevFiltX  = _filtX;
            _prevFiltY  = _filtY;
            if (_state == State::Touching &&
                _touchTicks <= static_cast<uint16_t>(Config::SETTLE_SAMPLES + 3)) {
                _startX = _filtX;
                _startY = _filtY;
            }
        }
        return GestureEvent::None;
    }

    _jumpPending = false;
    _lastRawX    = rawX;
    _lastRawY    = rawY;

    // Push sample into median-of-3 ring buffer
    _histX[_histIdx] = rawX;
    _histY[_histIdx] = rawY;
    _histIdx = (_histIdx + 1) % 3;
    if (_histFilled < 3) _histFilled++;

    float medX = static_cast<float>(rawX);
    float medY = static_cast<float>(rawY);
    if (_histFilled == 3) {
        medX = static_cast<float>(median3(_histX[0], _histX[1], _histX[2]));
        medY = static_cast<float>(median3(_histY[0], _histY[1], _histY[2]));
    }

    // Ongoing touch: apply EMA smoothing
    _filtX += Config::EMA_ALPHA * (medX - _filtX);
    _filtY += Config::EMA_ALPHA * (medY - _filtY);
    if (medY < _minSwipeY) {
        _minSwipeY = medY;
    }

    const float frameDx = _filtX - _prevFiltX;
    const float frameDy = _filtY - _prevFiltY;
    _prevFiltX = _filtX;
    _prevFiltY = _filtY;

    const float totalDx   = _filtX - _startX;
    const float totalDy   = _filtY - _startY;
    const float totalDist = hypotf(totalDx, totalDy);

    switch (_state) {
        case State::Touching:
            if (totalDist >= Config::MOVE_START_PX) {
                _tapEligible = false;
                _state       = State::Moving;
                accumulateRelativeMove(frameDx, frameDy, dtMs);
                flushRelativeMove(false, now);
            } else if (_tapEligible && (now - _touchDownMs >= longClickMs)) {
                // Deliberate stationary long-press -> Right Click
                _mouse->click(MOUSE_BTN_RIGHT);
                _state = State::ConsumedHold;
                Serial.println("[TP] LongPress -> RightClick");
                return GestureEvent::RightClick;
            }
            break;

        case State::SecondTouch:
            if (totalDist >= DRAG_START_PX) {
                // Finger is actively sliding -> press LEFT now and queue smooth accelerated drag motion
                _mouse->press(MOUSE_BTN_LEFT);
                _dragPressMs = now;
                _state       = State::Dragging;

                const float stepMag = hypotf(frameDx, frameDy);
                float speed = stepMag / static_cast<float>(dtMs > 0 ? dtMs : 8);
                if (speed > 4.0f) speed = 4.0f;
                const float gain = _settings->getBaseGain() * DRAG_GAIN_BOOST *
                                   (1.0f + _settings->getAccelFactor() * speed * 12.0f);

                _remX += totalDx * gain;
                _remY += totalDy * gain;

                Serial.printf("[TP] DragStart (dist=%.1f rem=%.1f,%.1f)\n", totalDist, _remX, _remY);
                return GestureEvent::DragStart;
            }
            break;

        case State::Moving:
            accumulateRelativeMove(frameDx, frameDy, dtMs);
            flushRelativeMove(false, now);
            break;

        case State::Dragging:
            accumulateRelativeMove(frameDx * DRAG_GAIN_BOOST, frameDy * DRAG_GAIN_BOOST, dtMs);
            if (now - _dragPressMs >= DRAG_PRESS_SETTLE_MS) {
                flushRelativeMove(true, now);
            }
            break;

        case State::ActionCorner: {
            const bool leftCornerBox =
                (rawX < (screenW - Config::VSCROLL_STRIP_WIDTH - 4)) ||
                (rawY < (screenH - Config::HSCROLL_STRIP_HEIGHT - 4));
            if (leftCornerBox && totalDist >= CORNER_SLIDE_OUT_PX) {
                // Deliberately slid out of corner along a scroll strip -> hand off to scrolling
                _cornerTapCount = 0;
                const bool hasRightStrip  = (cfg.scrollMode == ScrollMode::RightVertical ||
                                             cfg.scrollMode == ScrollMode::RightAndBottomBoth);
                const bool hasBottomStrip = (cfg.scrollMode == ScrollMode::RightAndBottomBoth);
                if (fabsf(totalDx) > fabsf(totalDy) && hasBottomStrip) {
                    _state = State::HScroll;
                    return emitHorizontalSwipeStep(cfg.naturalScroll);
                } else if (hasRightStrip) {
                    _state       = State::VScroll;
                    _scrollAccum = totalDy;
                    return emitScrollIfReady(now, true, cfg.naturalScroll);
                } else {
                    _tapEligible = false;
                    _state       = State::Moving;
                }
            } else if (_cornerTapCount == 0 && (now - _touchDownMs >= longClickMs)) {
                _state = State::ConsumedHold;
                Serial.printf("[TP] ActionCorner LongPress (%lu ms)\n", static_cast<unsigned long>(longClickMs));
                return triggerShortcutAction(cfg.longPressAction);
            }
            break;
        }

        case State::BottomStripPending: {
            const bool hasBottomStrip = (cfg.scrollMode == ScrollMode::RightAndBottomBoth);
            const float peakUpPx      = _startY - _minSwipeY;
            if ((totalDy <= -BOTTOM_SWIPE_ENGAGE_PX || peakUpPx >= 12.0f) &&
                peakUpPx >= fabsf(totalDx)) {
                _state            = State::BottomSwipeUp;
                _swipeHoldAnchorX = _filtX;
                _swipeHoldAnchorY = _filtY;
                _swipeHoldStartMs = 0;
                Serial.println("[TP] BottomSwipeUp engaged");
            } else if (fabsf(totalDx) >= 14.0f && fabsf(totalDx) > fabsf(totalDy) * 1.35f) {
                if (hasBottomStrip) {
                    _state = State::HScroll;
                    return emitHorizontalSwipeStep(cfg.naturalScroll);
                } else {
                    _tapEligible = false;
                    _state       = State::Moving;
                    accumulateRelativeMove(totalDx, totalDy, dtMs);
                    flushRelativeMove(false, now);
                }
            }
            break;
        }

        case State::BottomSwipeUp: {
            const float peakUpPx = _startY - _minSwipeY;
            if (peakUpPx >= 30.0f) {
                const float drift = hypotf(_filtX - _swipeHoldAnchorX, _filtY - _swipeHoldAnchorY);
                if (_swipeHoldStartMs == 0 || drift > 8.5f) {
                    _swipeHoldAnchorX = _filtX;
                    _swipeHoldAnchorY = _filtY;
                    _swipeHoldStartMs = now;
                } else if (now - _swipeHoldStartMs >= BOTTOM_SWIPE_HOLD_MS) {
                    // Swipe-up-and-pause -> open iOS App Switcher
                    _state = State::ConsumedHold;
                    Serial.println("[TP] BottomSwipeUp + Hold -> AppSwitcher");
                    return triggerShortcutAction(ShortcutAction::AppSwitcher);
                }
            }
            break;
        }

        case State::ScrollCorner:
            if (fabsf(totalDx) >= 4.0f || fabsf(totalDy) >= 4.0f) {
                if (fabsf(totalDx) > fabsf(totalDy)) {
                    _state = State::HScroll;
                    return emitHorizontalSwipeStep(cfg.naturalScroll);
                } else {
                    _state       = State::VScroll;
                    _scrollAccum = totalDy;
                    return emitScrollIfReady(now, true, cfg.naturalScroll);
                }
            }
            break;

        case State::VScroll:
            _scrollAccum += frameDy;
            return emitScrollIfReady(now, true, cfg.naturalScroll);

        case State::HScroll:
            return emitHorizontalSwipeStep(cfg.naturalScroll);

        case State::CornerRightTap:
            if (totalDist > Config::TAP_SLOP_PX * 1.5f) {
                // Slid out of corner -> treat as normal move instead of accidental right-click
                _tapEligible = false;
                _state       = State::Moving;
            }
            break;

        default:
            break;
    }

    return GestureEvent::None;
}

void TouchpadEngine::accumulateRelativeMove(float rawDx, float rawDy, uint32_t dtMs) {
    const float stepMag = hypotf(rawDx, rawDy);
    if (stepMag < 0.08f) return;

    // Velocity in px/ms (clamped for stability)
    float speed = stepMag / static_cast<float>(dtMs > 0 ? dtMs : 8);
    if (speed > 4.0f) speed = 4.0f;

    const float gain = _settings->getBaseGain() * (1.0f + _settings->getAccelFactor() * speed * 12.0f);

    _remX += rawDx * gain;
    _remY += rawDy * gain;
}

void TouchpadEngine::flushRelativeMove(bool isDrag, uint32_t now) {
    if (isDrag && _lastDragReportMs != 0 && (now - _lastDragReportMs < DRAG_REPORT_INTERVAL_MS)) {
        return;
    }

    int16_t outX = static_cast<int16_t>(truncf(_remX));
    int16_t outY = static_cast<int16_t>(truncf(_remY));

    if (outX == 0 && outY == 0) {
        return;
    }

    if (isDrag) {
        if (outX > MAX_DRAG_STEP_DELTA)  outX = MAX_DRAG_STEP_DELTA;
        if (outX < -MAX_DRAG_STEP_DELTA) outX = -MAX_DRAG_STEP_DELTA;
        if (outY > MAX_DRAG_STEP_DELTA)  outY = MAX_DRAG_STEP_DELTA;
        if (outY < -MAX_DRAG_STEP_DELTA) outY = -MAX_DRAG_STEP_DELTA;
        _lastDragReportMs = now;
    }

    _remX -= outX;
    _remY -= outY;
    _mouse->move(outX, outY, 0, 0);
}

GestureEvent TouchpadEngine::emitScrollIfReady(uint32_t now, bool vertical, bool naturalScroll) {
    if (_lastScrollReportMs != 0 && (now - _lastScrollReportMs < SCROLL_REPORT_INTERVAL_MS)) {
        return GestureEvent::None;
    }

    const int steps = static_cast<int>(_scrollAccum / SCROLL_PIXELS_PER_STEP);
    if (steps == 0) {
        return GestureEvent::None;
    }

    _scrollAccum -= static_cast<float>(steps) * SCROLL_PIXELS_PER_STEP;
    _lastScrollReportMs = now;

    const int absSteps = abs(steps);
    int mag = absSteps * SCROLL_UNITS_PER_STEP + (absSteps > 1 ? (absSteps - 1) * 2 : 0);
    if (mag > 36) mag = 36;
    const int signedDelta = (steps > 0) ? mag : -mag;

    if (vertical) {
        // Traditional mouse wheel: sliding finger down scrolls content down (negative wheel)
        const int8_t wheel = static_cast<int8_t>(naturalScroll ? signedDelta : -signedDelta);
        _mouse->move(0, 0, wheel, 0);
    } else {
        // Horizontal AC Pan (0x0238): positive pans right, negative pans left
        const int8_t pan = static_cast<int8_t>(naturalScroll ? -signedDelta : signedDelta);
        _mouse->move(0, 0, 0, pan);
    }

    return GestureEvent::Scrolling;
}

GestureEvent TouchpadEngine::emitHorizontalSwipeStep(bool naturalScroll) {
    const float dx = _filtX - _hScrollAnchorX;
    if (_hScrollFiredDir == 0) {
        if (fabsf(dx) < HSCROLL_SWIPE_TRIGGER_PX) {
            return GestureEvent::None;
        }
        const int8_t dir         = (dx > 0.0f) ? 1 : -1;
        _hScrollFiredDir         = dir;
        _hScrollAnchorX          = _filtX;
        const int8_t signedUnits = static_cast<int8_t>(dir * HSCROLL_PAGE_UNITS);
        const int8_t pan         = naturalScroll ? static_cast<int8_t>(-signedUnits) : signedUnits;
        _mouse->move(0, 0, 0, pan);
        Serial.printf("[TP] HScroll page step (dir=%d pan=%d)\n", dir, pan);
        return GestureEvent::Scrolling;
    }

    // Already stepped once on this swipe: keep tracking furthest excursion in the same direction
    // so even a very slow swipe across the entire bar only switches 1 Home Screen page.
    if ((_hScrollFiredDir > 0 && _filtX > _hScrollAnchorX) ||
        (_hScrollFiredDir < 0 && _filtX < _hScrollAnchorX)) {
        _hScrollAnchorX = _filtX;
        return GestureEvent::None;
    }

    // Allow reversing direction mid-touch if the finger deliberately sweeps >= 26 px the other way
    if (fabsf(dx) >= (HSCROLL_SWIPE_TRIGGER_PX + 6.0f)) {
        const int8_t revDir = (dx > 0.0f) ? 1 : -1;
        if (revDir != _hScrollFiredDir) {
            _hScrollFiredDir         = revDir;
            _hScrollAnchorX          = _filtX;
            const int8_t signedUnits = static_cast<int8_t>(revDir * HSCROLL_PAGE_UNITS);
            const int8_t pan         = naturalScroll ? static_cast<int8_t>(-signedUnits) : signedUnits;
            _mouse->move(0, 0, 0, pan);
            Serial.printf("[TP] HScroll reverse page step (dir=%d pan=%d)\n", revDir, pan);
            return GestureEvent::Scrolling;
        }
    }

    return GestureEvent::None;
}

GestureEvent TouchpadEngine::resolveCornerTapCount(uint8_t tapCount, const TouchpadSettings& cfg) {
    ShortcutAction act = ShortcutAction::None;
    if (tapCount == 1) {
        act = cfg.singleTapAction;
    } else if (tapCount == 2) {
        act = cfg.doubleTapAction;
    } else if (tapCount >= 3) {
        act = cfg.tripleTapAction;
    }
    return triggerShortcutAction(act);
}

GestureEvent TouchpadEngine::triggerShortcutAction(ShortcutAction action) {
    if (action == ShortcutAction::None) {
        return GestureEvent::None;
    }

    _lastShortcut = action;

    switch (action) {
        case ShortcutAction::Home:
            _mouse->consumerClick(CONSUMER_MENU);
            return GestureEvent::ShortcutTriggered;
        case ShortcutAction::AppSwitcher:
            _mouse->doubleHome();
            return GestureEvent::ShortcutTriggered;
        case ShortcutAction::Spotlight:
            _mouse->consumerClick(CONSUMER_AC_SEARCH);
            return GestureEvent::ShortcutTriggered;
        case ShortcutAction::Siri:
            _mouse->consumerClick(CONSUMER_VOICE_CMD);
            return GestureEvent::ShortcutTriggered;
        case ShortcutAction::LockScreen:
            _mouse->consumerClick(CONSUMER_POWER);
            return GestureEvent::ShortcutTriggered;
        case ShortcutAction::PlayPause:
            _mouse->consumerClick(CONSUMER_PLAY_PAUSE);
            return GestureEvent::ShortcutTriggered;
        case ShortcutAction::RightClick:
            _mouse->click(MOUSE_BTN_RIGHT);
            return GestureEvent::RightClick;
        case ShortcutAction::MiddleClick:
            _mouse->click(MOUSE_BTN_MIDDLE);
            return GestureEvent::ShortcutTriggered;
        case ShortcutAction::Button4:
            _mouse->click(MOUSE_BTN_BACK);
            return GestureEvent::ShortcutTriggered;
        case ShortcutAction::Button5:
            _mouse->click(MOUSE_BTN_FORWARD);
            return GestureEvent::ShortcutTriggered;
        case ShortcutAction::OpenMenu:
            return GestureEvent::OpenSettings;
        default:
            return GestureEvent::None;
    }
}
