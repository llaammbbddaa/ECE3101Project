# How macros work in `Macros_Trial1_updated.ino`

The [macro trial sketch](../Macros_Trial1_updated.ino) uses the Extra button (S9 on D10) to trigger fixed sequences of USB gamepad button presses. Each sequence repeats a single button. Extra is reserved for triggering macros, so pressing it produces no Extra button input on the host.

| Extra taps | Gamepad output | Zero-based HID button index |
| --- | --- | --- |
| 1 | A twice | 0 |
| 2 | B twice | 1 |
| 3 or more | X three times | 3 |

## How Extra taps select a sequence

The main loop targets one scan per millisecond and debounces both edges of each button for 8 ms. A debounced Extra press arms the tap. Its debounced release counts the tap and updates `lastExtraTapAt`. The counter caps at three, and each additional completed tap still restarts the timer.

The sequence starts once 250 ms have passed since the last counted release and Extra is released. If Extra is held down when the window would expire, selection waits for the release and the full window starts again. An Extra press that is debounced while a macro is active is ignored, even if its release comes after playback ends.

## How playback reaches the host

`startMacro()` translates the tap count into a HID button index and a repeat count, clears the tap counter, and starts the first virtual press immediately. On later scans, `updateMacro()` releases the virtual button after 80 ms and decrements the repeat count. If presses remain, it waits 200 ms before pressing again. The macro ends on the final release, so there is no trailing gap. The nominal playback time is 360 ms for A or B and 640 ms for X, measured from the first virtual press to the final release.

The timing uses elapsed `millis()` checks, so the loop keeps scanning physical buttons and sticks during playback. `updateButtons()` combines the active virtual press with the debounced physical button states before the loop sends a batched USB HID report. Either source can hold a button down. For example, if you physically hold A during an A macro, the host sees A stay down through the virtual release gaps.

To change a sequence's target or repeat count, edit the corresponding branch in `startMacro()`. The shared timing constants at the top of the sketch are `TAP_WINDOW_MS`, `MACRO_PRESS_MS`, and `MACRO_GAP_MS`.

## Earlier design notes

- Some games require exceptionally precise inputs to be done in fast succession, it would be nice to add the ability to just press a button and have it perform the sequence of inputs for you.
- Make D-pad perform some combination of movements
  - Do we want all buttons to be designated normal inputs or do we want to have the d-pad just be for macros?

Sites for help with Macros
- https://littlebirdelectronics.com.au/projects/macro-keypad-with-atmega32u4?srsltid=AYCX9vI6rp70l8AysFwEGFNlJSOTKqI98ed2GWlOUsHd1I3LljR_8iXy
- https://docs.arduino.cc/built-in-examples/digital/BlinkWithoutDelay/
- https://docs.arduino.cc/built-in-examples/digital/Debounce/
- https://docs.arduino.cc/built-in-examples/digital/StateChangeDetection/
- https://github.com/NicoHood/HID
