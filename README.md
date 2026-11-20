Pro Micro USB gamepad

Open gamecube.ino in Arduino IDE. Install [SparkFun AVR Boards](https://learn.sparkfun.com/tutorials/pro-micro--fio-v3-hookup-guide)
and select SparkFun Pro Micro. Choose the processor option that matches your
module (5 V / 16 MHz or 3.3 V / 8 MHz).

Install MHeironimus's [ArduinoJoystickLibrary](https://github.com/MHeironimus/ArduinoJoystickLibrary)
for Arduino IDE, or use the bundled library with Arduino CLI.

### PCB connections

- Switches S1 through S9 connect to D2 through D10, respectively. Each
  normally open switch pulls its pin to ground when pressed. The firmware
  enables the Pro Micro's internal pull-ups.
- Each stick header has five pads. From the square pad, the left header
  routes to GND, VCC, A1, A0, and an unconnected pad. The right header
  routes to GND, VCC, A2, A3, and an unconnected pad. (**the unconnected switch is the joystick's button that isn't really useful**)
- The firmware assumes the joystick module uses GND, VCC, VRx, VRy, SW in
  that order. Check the labels on your modules before installing them.
  The joystick push switches are unused.
- The firmware reads left X/Y from A0/A1 and right X/Y from A2/A3.

Upload with sticks resting at center. Keep them untouched for the first
approximately 200 ms after every reset or power-up. Startup calibration averages
50 readings per axis; it measures center only and assumes ADC endpoints 0/1023.
Reset with sticks released to recalibrate. Limited mechanical travel may require
additional endpoint calibration for full-scale output.

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

### Compile and upload with Arduino CLI

Install the Pro Micro board package once:

```bash
arduino-cli core install SparkFun:avr --additional-urls https://raw.githubusercontent.com/sparkfun/Arduino_Boards/main/IDE_Board_Manager/package_sparkfun_index.json
```

For a 5 V / 16 MHz Pro Micro, run this from the project directory:

```bash
arduino-cli compile --upload --fqbn SparkFun:avr:promicro:cpu=16MHzatmega32U4 --port /dev/ttyACM0 --library ArduinoJoystickLibrary-master .
```

For a 3.3 V / 8 MHz module, use `cpu=8MHzatmega32U4`. Run
`arduino-cli board list` to check the USB port. Close Serial Monitor before
uploading and leave both sticks centered during reset.

### Capture button assignments on Linux

With the gamepad connected and running the current firmware, run:

```bash
python3 configure_buttons.py
```

Press and release the button named in each prompt. The default labels are
A, B, X, Y, L, R, Z, Start, and Extra. To capture a subset or use different
labels, pass them in prompt order, for example
`python3 configure_buttons.py A B X Y`. If automatic detection fails, pass
the gamepad's event device with `--device /dev/input/eventN`.

The tool writes `button_mapping.json` after the last button is released.
It records the current zero-based HID index for each label. Use the saved
file to identify which entries in the Arduino sketch's button mapping need
to change.

### testing

i would use [this website](https://hardwaretester.com/gamepad) to quickly test changes
