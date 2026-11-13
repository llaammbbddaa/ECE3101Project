#include <Arduino.h>
#include <Joystick.h>

#define DEBUG 0  // Set to 1 for occasional Serial output; never waits for Serial.

constexpr uint8_t AXIS_COUNT = 4;
enum StickAxis : uint8_t { LEFT_X, LEFT_Y, RIGHT_X, RIGHT_Y };
constexpr uint8_t BUTTON_COUNT = 4;
// Physical stick wiring: left X/Y on A0/A1, right X/Y on A2/A3.
constexpr uint8_t AXIS_PINS[AXIS_COUNT] = {A0, A1, A3, A2};
constexpr uint8_t BUTTON_PINS[BUTTON_COUNT] = {4, 5, 6, 7};
// Library indices are zero-based HID usages. Skip index 2 (Linux BTN_C).
// Pins 4/5/6/7 report SOUTH/EAST/WEST/NORTH (browser B0/B1/B2/B3).
constexpr uint8_t BUTTON_REPORT_INDEX[BUTTON_COUNT] = {0, 1, 4, 3};
constexpr uint8_t HID_BUTTON_COUNT = 5;  // Include index 4; index 2 stays released.
// Order: left X, left Y, right X, right Y.
constexpr bool INVERT_AXIS[AXIS_COUNT] = {false, true, false, false};
constexpr uint16_t ADC_MAX = 1023;
constexpr int16_t AXIS_LIMIT = 32767;  // Library range: -32767 ... +32767.
constexpr uint8_t DEADZONE_PERCENT = 5;  // 5% of each side's travel from center.
constexpr uint16_t DEBOUNCE_MS = 8;
constexpr uint16_t UPDATE_INTERVAL_MS = 1;  // Target 1000 scans/second.
constexpr uint16_t CALIBRATION_SAMPLES = 50;
constexpr uint16_t CALIBRATION_INTERVAL_MS = 2;
constexpr uint16_t STARTUP_SETTLE_MS = 100;
constexpr uint16_t DEBUG_INTERVAL_MS = 250;
constexpr unsigned long DEBUG_BAUD = 115200;
static_assert(DEADZONE_PERCENT < 100, "Deadzone must be less than 100%");
static_assert(CALIBRATION_SAMPLES > 0, "Calibration needs samples");

// One gamepad, four physical buttons, five HID button slots, no hats.
// Left stick uses HID X/Y; right stick uses HID Z/Rz.
Joystick_ Gamepad(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD,
                 HID_BUTTON_COUNT, 0,
                 true, true, true, false, false, true,
                 false, false, false, false, false);

uint32_t centerSum[AXIS_COUNT] = {};
uint16_t center[AXIS_COUNT] = {};
int16_t axisValue[AXIS_COUNT] = {};
bool rawPressed[BUTTON_COUNT] = {};
bool stablePressed[BUTTON_COUNT] = {};
uint32_t buttonChangedAt[BUTTON_COUNT] = {};
uint32_t startupAt;
uint32_t lastCalibrationAt;
uint32_t lastScanAt;
uint16_t calibrationCount = 0;
bool calibrated = false;
bool forceReport = true;

// Rescale each half separately so the measured center maps to zero and
// ADC endpoints still reach full scale. Remove the deadzone continuously.
int16_t mapAxis(uint16_t raw, uint16_t midpoint, bool invert) {
  const int32_t delta = int32_t(raw) - midpoint;
  const int32_t distance = delta < 0 ? -delta : delta;
  const int32_t span = delta < 0 ? midpoint : ADC_MAX - midpoint;
  const int32_t deadzone = span * DEADZONE_PERCENT / 100;
  if (distance <= deadzone || span <= deadzone) return 0;

  int32_t result = (distance - deadzone) * AXIS_LIMIT / (span - deadzone);
  if (result > AXIS_LIMIT) result = AXIS_LIMIT;
  if (delta < 0) result = -result;
  if (invert) result = -result;
  return int16_t(result);
}

void setup() {
#if DEBUG
  Serial.begin(DEBUG_BAUD);
#endif
  analogReference(DEFAULT);  // 5 V reference with a normally powered Micro.
  for (uint8_t i = 0; i < AXIS_COUNT; ++i) pinMode(AXIS_PINS[i], INPUT);
  for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    rawPressed[i] = (digitalRead(BUTTON_PINS[i]) == LOW);
    // stablePressed starts released; even a held startup button is debounced.
  }

  Gamepad.setXAxisRange(-AXIS_LIMIT, AXIS_LIMIT);
  Gamepad.setYAxisRange(-AXIS_LIMIT, AXIS_LIMIT);
  Gamepad.setZAxisRange(-AXIS_LIMIT, AXIS_LIMIT);
  Gamepad.setRzAxisRange(-AXIS_LIMIT, AXIS_LIMIT);
  Gamepad.setXAxis(0);
  Gamepad.setYAxis(0);
  Gamepad.setZAxis(0);
  Gamepad.setRzAxis(0);
  Gamepad.begin(false);  // Explicit, batched reports.

  startupAt = millis();
  lastCalibrationAt = startupAt;
  lastScanAt = startupAt;
  for (uint8_t i = 0; i < BUTTON_COUNT; ++i) buttonChangedAt[i] = startupAt;
}

void loop() {
  const uint32_t now = millis();
  // Unsigned subtraction keeps scheduling correct across millis() rollover.
  if (uint32_t(now - lastScanAt) < UPDATE_INTERVAL_MS) return;
  lastScanAt = now;  // Skip missed ticks; do not send bursts to catch up.
  bool changed = forceReport;

  for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
    const bool pressed = (digitalRead(BUTTON_PINS[i]) == LOW);
    if (pressed != rawPressed[i]) {
      rawPressed[i] = pressed;
      buttonChangedAt[i] = now;
    }
    if (rawPressed[i] != stablePressed[i] &&
        uint32_t(now - buttonChangedAt[i]) >= DEBOUNCE_MS) {
      stablePressed[i] = rawPressed[i];
      Gamepad.setButton(BUTTON_REPORT_INDEX[i], stablePressed[i]);
      changed = true;
    }
  }

  if (!calibrated) {
    // Leave both sticks untouched for approximately the first 200 ms.
    // No delay(): sample 50 times, spaced 2 ms apart after settling.
    if (uint32_t(now - startupAt) >= STARTUP_SETTLE_MS &&
        uint32_t(now - lastCalibrationAt) >= CALIBRATION_INTERVAL_MS) {
      lastCalibrationAt = now;
      for (uint8_t i = 0; i < AXIS_COUNT; ++i) {
        centerSum[i] += analogRead(AXIS_PINS[i]);
      }
      if (++calibrationCount == CALIBRATION_SAMPLES) {
        for (uint8_t i = 0; i < AXIS_COUNT; ++i) {
          center[i] = (centerSum[i] + CALIBRATION_SAMPLES / 2) /
                      CALIBRATION_SAMPLES;
          // Guard degenerate endpoints against division by zero.
          center[i] = constrain(center[i], 1, ADC_MAX - 1);
        }
        calibrated = true;
      }
    }
  } else {
    for (uint8_t i = 0; i < AXIS_COUNT; ++i) {
      const int16_t value = mapAxis(analogRead(AXIS_PINS[i]), center[i],
                                    INVERT_AXIS[i]);
      if (value != axisValue[i]) {
        axisValue[i] = value;
        changed = true;
      }
    }
  }

  if (changed) {
    Gamepad.setXAxis(axisValue[LEFT_X]);
    Gamepad.setYAxis(axisValue[LEFT_Y]);
    Gamepad.setZAxis(axisValue[RIGHT_X]);
    Gamepad.setRzAxis(axisValue[RIGHT_Y]);
    Gamepad.sendState();
    forceReport = false;
  }

#if DEBUG
  static uint32_t lastDebugAt = 0;
  // Keep debug writes small enough to fit the USB serial TX buffer.
  // Diagnostics are optional and can still affect timing when enabled.
  if (Serial && Serial.availableForWrite() >= 60 &&
      uint32_t(now - lastDebugAt) >= DEBUG_INTERVAL_MS) {
    lastDebugAt = now;
    Serial.print(calibrated ? F("axes: ") : F("calibrating: "));
    for (uint8_t i = 0; i < AXIS_COUNT; ++i) {
      Serial.print(axisValue[i]);
      Serial.print(' ');
    }
    Serial.print(F("buttons: "));
    for (uint8_t i = 0; i < BUTTON_COUNT; ++i) Serial.print(stablePressed[i]);
    Serial.println();
  }
#endif
}
