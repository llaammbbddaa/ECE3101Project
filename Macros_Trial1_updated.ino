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
// Pins 2–10 are labeled A, B, X, Y, R, Z, L, Start, Extra.
constexpr uint8_t BUTTON_REPORT_INDEX[BUTTON_COUNT] = {0, 1, 3, 4, 7, 8, 6, 11, 10};
constexpr uint8_t HID_BUTTON_COUNT = 12;  // Includes Start at index 11.
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

// One gamepad, nine physical buttons, twelve HID button slots, no hats.
// Left stick uses HID X/Y; right stick uses HID Z/Rz.
Joystick_ Gamepad(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD,
                 HID_BUTTON_COUNT, 0,
                 true, true, true, false, false, true,
                 false, false, false, false, false);

//Macros 
constexpr uint8_t EXTRA_BUTTON = 8;
constexpr uint16_t TAP_WINDOW_MS = 250; // waits for macro presses if more than one for this long
constexpr uint16_t MACRO_PRESS_MS = 80; // how long each button stays pressed during macro sequence
constexpr uint16_t MACRO_GAP_MS = 200; // release time between virtual presses

//Extra button taps
uint8_t extraTapCount = 0;
bool extraArmed = false; // Tracks whether Extra was pressed, waiting for release
uint32_t lastExtraTapAt = 0;

//Store current macro state
bool macroActive = false;// true while macro starts
bool macroHolding = false;//true while virtual button should be pressed
uint8_t macroButton = 0; //button index selected by the macro
uint8_t macroRemaining = 0; //number of macros remaining in execution 
uint32_t macroChangedAt = 0; //time when the current press or release began

//Store the last reported HID state to avoid unecessary USB updates
bool reportedPressed[HID_BUTTON_COUNT] = {};

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

//Start sequence
void startMacro(uint32_t now) {
  if (macroActive || extraTapCount == 0) return; //don't start another macro if one is active

  if (extraTapCount == 1) {
    macroButton = BUTTON_REPORT_INDEX[0]; // tap once, button A pressed twice
    macroRemaining = 2;
  } else if (extraTapCount == 2) {
    macroButton = BUTTON_REPORT_INDEX[1]; // tap twice, button B pressed twice
    macroRemaining = 2;
  } else {
    macroButton = BUTTON_REPORT_INDEX[2]; // tap 3 times, button 'X' pressed three times
    macroRemaining = 3; 
  }

  //reset tap counter
  extraTapCount = 0;
  macroActive = true;
  macroHolding = true;
  macroChangedAt = now;
}

//control timing of macro button press and release
void updateMacro(uint32_t now) {
  //exit if no macro is currently running
  if (!macroActive) return;

  
  if (macroHolding) {
    //check if virtual button has been helf for the required duration
    if (uint32_t(now - macroChangedAt) >= MACRO_PRESS_MS) {
      macroHolding = false; //release  the virtual button
      macroChangedAt = now; //record when the release phase begins
      macroRemaining--; //decrease the remaining number of presses

      //stop the macro once presses are completed
      if (macroRemaining == 0) {
        macroActive = false;
      }
    }
  } else {
    //wait for release interval before starting next press
    if (uint32_t(now - macroChangedAt) >= MACRO_GAP_MS) {
      macroHolding = true; //begin the next virtual button press
      macroChangedAt = now;
    }
  }
}

//combine physical button inputs and macro outputs, into HID button states
bool updateButtons() {
  bool changed = false;
  bool desired[HID_BUTTON_COUNT] = {};

  //Preserve normal functionality while excluding 'Extra'
  for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
    if (i != EXTRA_BUTTON && stablePressed[i]) {
      desired[BUTTON_REPORT_INDEX[i]] = true;
    }
  }

  //Add the virtual button press generated by an active macro
  if (macroActive && macroHolding) {
    desired[macroButton] = true;
  }

  //update only HID buttons whose states have changed
  for (uint8_t i = 0; i < HID_BUTTON_COUNT; ++i) {
    if (desired[i] != reportedPressed[i]) {
      reportedPressed[i] = desired[i];
      Gamepad.setButton(i, desired[i]); //update the virtual gamepad button
      changed = true;
    }
  }

  return changed;
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

      if (i == EXTRA_BUTTON) {
        if (stablePressed[i]) {
          if (!macroActive) extraArmed = true;
        } else {
          if (extraArmed && !macroActive) {
            if (extraTapCount < 3) extraTapCount++;
            lastExtraTapAt = now;
          }
          extraArmed = false;
        }
      }

      changed = true;
    }
  }

  //Execute the selected macro after the tap window expires
  if (!macroActive && extraTapCount > 0 &&
      !stablePressed[EXTRA_BUTTON] && !extraArmed &&
      uint32_t(now - lastExtraTapAt) >= TAP_WINDOW_MS) {
    startMacro(now);
  }

  //Advance the macro and update the virtual gamepad button states
  updateMacro(now);
  if (updateButtons()) changed = true;

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

