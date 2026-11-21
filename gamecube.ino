#include <Arduino.h>
#include <Joystick.h>

#define DEBUG 0  // Set to 1 for occasional Serial output; never waits for Serial.

constexpr uint8_t AXIS_COUNT = 4;
enum StickAxis : uint8_t { LEFT_X, LEFT_Y, RIGHT_X, RIGHT_Y };
constexpr uint8_t BUTTON_COUNT = 9;
// Firmware axis inputs in order: left X, left Y, right X, right Y.
constexpr uint8_t AXIS_PINS[AXIS_COUNT] = {A0, A1, A2, A3};
// PCB switches S1 through S9, in silkscreen order.
constexpr uint8_t BUTTON_PINS[BUTTON_COUNT] = {2, 3, 4, 5, 6, 7, 8, 9, 10};
// Library indices are zero-based HID usages. Skip index 2 (Linux BTN_C).
// Pins 2-10 are labeled A, B, X, Y, R, Z, L, Start, Extra.
constexpr uint8_t BUTTON_REPORT_INDEX[BUTTON_COUNT] = {0, 1, 3, 4, 7, 8, 6, 11, 10};
constexpr uint8_t HID_BUTTON_COUNT = 12;  // Includes Start at index 11.
// Order: left X, left Y, right X, right Y.
constexpr bool INVERT_AXIS[AXIS_COUNT] = {false, true, false, false};
constexpr uint16_t ADC_MAX = 1023;
constexpr int16_t AXIS_LIMIT = 32767;  // Library range: -32767 ... +32767.
constexpr uint8_t DEADZONE_PERCENT = 5;  // 5% of each side's travel from center.
constexpr uint16_t DEBOUNCE_MS = 8;

// SNAPBACK ADDED: Protection settings
constexpr int16_t SNAP_ARM = 18000;
constexpr int16_t SNAP_CENTER = 5000;
constexpr int16_t SNAP_LIMIT = 12000;
constexpr uint16_t SNAP_TIME_MS = 25;

constexpr uint16_t UPDATE_INTERVAL_MS = 1;  // Target 1000 scans/second.
constexpr uint16_t CALIBRATION_SAMPLES = 50;
constexpr uint16_t CALIBRATION_INTERVAL_MS = 2;
constexpr uint16_t STARTUP_SETTLE_MS = 100;
constexpr uint16_t DEBUG_INTERVAL_MS = 250;
constexpr unsigned long DEBUG_BAUD = 115200;
static_assert(DEADZONE_PERCENT < 100, "Deadzone must be less than 100%");
static_assert(CALIBRATION_SAMPLES > 0, "Calibration needs samples");

// One gamepad, nine physical buttons, twelve HID button slots, no hats.
// Left stick uses HID X/Y; right stick uses HID Z/Rz.
Joystick_ Gamepad(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD,
                 HID_BUTTON_COUNT, 0,
                 true, true, true, false, false, true,
                 false, false, false, false, false);

// Original variables
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

// SNAPBACK ADDED: State for each joystick axis
int8_t snapDirection[AXIS_COUNT] = {};
bool snapArmed[AXIS_COUNT] = {};
bool snapWatching[AXIS_COUNT] = {};
uint32_t snapStart[AXIS_COUNT] = {};

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

// SNAPBACK ADDED: Detect and suppress brief joystick snapback
int16_t protectSnapback(uint8_t axis, int16_t value, uint32_t now) {
  if (value >= SNAP_ARM) {
    snapDirection[axis] = 1;
    snapArmed[axis] = true;
    snapWatching[axis] = false;
  }
  else if (value <= -SNAP_ARM) {
    snapDirection[axis] = -1;
    snapArmed[axis] = true;
    snapWatching[axis] = false;
  }

  if (snapArmed[axis] && abs(value) <= SNAP_CENTER) {
    snapStart[axis] = now;
    snapWatching[axis] = true;
    snapArmed[axis] = false;
  }

  if (snapWatching[axis]) {
    if (uint32_t(now - snapStart[axis]) > SNAP_TIME_MS) {
      snapWatching[axis] = false;
    }
    else {
      if ((snapDirection[axis] == 1 &&
           value < 0 && value >= -SNAP_LIMIT) ||
          (snapDirection[axis] == -1 &&
           value > 0 && value <= SNAP_LIMIT)) {
        return 0;
      }
    }
  }

  return value;
}

void setup() {
#if DEBUG
  Serial.begin(DEBUG_BAUD);
#endif
  analogReference(DEFAULT);  // The joystick headers and ADC reference use VCC.
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

      // SNAPBACK ADDED: Allow the axis value to be filtered
      int16_t value = mapAxis(analogRead(AXIS_PINS[i]), center[i],
                              INVERT_AXIS[i]);

      // SNAPBACK ADDED: Apply snapback protection
      value = protectSnapback(i, value, now);

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
