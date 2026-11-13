Arduino Micro USB gamepad

Open gamecube.ino in Arduino IDE (keep it in the gamecube folder).
Install Arduino AVR Boards through
Boards Manager if needed, then select Tools > Board > Arduino AVR Boards >
Arduino Micro and select its USB port.

Install MHeironimus's ArduinoJoystickLibrary (Joystick.h):
https://github.com/MHeironimus/ArduinoJoystickLibrary
Download Code > Download ZIP, then use Sketch > Include Library > Add .ZIP
Library in Arduino IDE. Use this library rather than another similarly named
Joystick library.

Wiring:
- Both joystick modules: VCC to Micro 5V, GND to Micro GND.
- Left joystick VRx/wiper to A0, VRy/wiper to A1.
- Right joystick VRx/wiper to A2, VRy/wiper to A3.
- Each button connects its assigned pin (4, 5, 6, or 7) to GND when pressed.
  Internal pull-ups are enabled; no external pull-up resistors are needed.
- For bare potentiometers: outer terminals to 5V and GND, wiper to its axis pin.
- Joystick module push-switch terminals are unused.

Upload with sticks resting at center. Keep them untouched for the first
approximately 200 ms after every reset or power-up. Startup calibration averages
50 readings per axis; it measures center only and assumes ADC endpoints 0/1023.
Reset with sticks released to recalibrate. Limited mechanical travel may require
additional endpoint calibration for full-scale output.

Test on Windows: Win+R > joy.cpl > select the controller > Properties > Test.
Move each stick independently and press each button. Left stick controls X/Y;
right controls Z/Rz. Pins 4/5/6/7 use library button indices 0/1/4/3.
This skips Linux BTN_C so the buttons report SOUTH/EAST/WEST/NORTH,
normally B0/B1/B2/B3 in Chromium on Linux. Five HID button slots are exposed;
index 2 remains released. Other hosts may show raw indices or one-based numbers.
Alternatively, open https://gamepad-tester.com and press a button to activate it.
Check that sticks settle at center with no reported drift. Axis names and game
bindings depend on the host/game; assign Z/Rz to right-stick movement if needed.
This is a generic USB HID gamepad; games requiring XInput may need a mapper.

All pins/tuning are at the top of the sketch. Set an INVERT_AXIS entry to true
if that axis moves in the opposite direction. The default deadzone is 5% of
travel from center toward each endpoint (roughly +/-26 ADC counts at center
512), rescaled continuously outside the deadzone. It is per-axis, not radial.
Buttons must remain stable for 8 ms before a press/release is reported.

The millis() scheduler targets one scan per millisecond. Four analogRead calls
normally fit inside that budget on a Micro. Reports are batched and sent only
when mapped axes or debounced buttons change, plus an initial report. ADC noise
outside the deadzone can therefore produce frequent reports. Actual rate depends
on USB/host scheduling; the library's USB send can block, so this is cooperative
scheduling without delay(), not a hard real-time guarantee.

DEBUG defaults to 0. Set it to 1 and open Serial Monitor at 115200 for occasional
axis/button diagnostics. It never waits for Serial; debug USB traffic can affect
timing.

Compile and flash with arduino-cli:

```bash
./flash.sh                 # Detect exactly one Arduino Micro (uses python3)
./flash.sh /dev/ttyACM0     # Or specify the port from arduino-cli board list
```

Install the board core first if needed: `arduino-cli core update-index`, then
`arduino-cli core install arduino:avr`. The script uses the bundled
`ArduinoJoystickLibrary-master` when present; otherwise install the Joystick
library in your Arduino sketchbook. It compiles before uploading and verifies
the flash. Keep the sticks centered during startup. Close Serial Monitor before
uploading. Temporary build files are removed automatically.
