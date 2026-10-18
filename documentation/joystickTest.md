Dual Joystick Input Test — Arduino UNO R3
ECE 3301 | USB GameCube Controller | Group 8
Overview
This initial prototype uses an ELEGOO UNO R3 and two analog joystick modules to test how the microcontroller reads analog movement and digital button inputs. Both joysticks were connected and programmed to display their X-axis, Y-axis, and button states in the Arduino IDE Serial Monitor.
Hardware Setup
Components: ELEGOO UNO R3, two analog joystick modules, breadboard, jumper wires, and USB cable.
Joystick Pin	      Joystick 1	      Joystick 2
VCC	                    5V	              5V
GND	                    GND              	GND
VRx	                    A0	              A2
VRy	                    A1              	A3
SW	                    D2	              D3
Both joysticks share the Arduino’s 5V and GND through the breadboard.
How the Code Works
analogRead() reads the X/Y positions using the 10-bit ADC (values 0–1023).
digitalRead() checks each joystick push button using active-low logic with INPUT_PULLUP.
Serial.begin(9600) initializes serial communication; readings are printed once per second.
Testing and Results
Each joystick was tested in all four directions and by pressing its built-in button.
Test	                      Joystick 1	                  Joystick 2
Center	                  X: 496, Y: 504	              X: 528, Y: 482
Left	                          X: 0                      	X: 0
Right	                        X: 1023                    	X: 1023
Up	                            Y: 0	                      Y: 0
Down	                        Y: 1023	                    Y: 1023
Button press	                Detected	                  Detected
Result: All tests passed. Both joysticks correctly detected movement and button presses.
Observations
Neither joystick rests at exactly 512, the approximate center value. Calibration and deadzone filtering can account for these offsets. The Arduino successfully sent input readings to the computer through USB serial communication, but USB HID gamepad functionality has not yet been implemented.
Next Steps
Implement joystick calibration and deadzone filtering.
Test snapback filtering.
Add physical buttons and triggers.
Research USB HID compatibility for the final controller.
Combine inputs into a working controller prototype.
Current Status
Completed: USB serial communication, dual joystick wiring, analog input processing, digital button detection, and directional testing. Next: USB gamepad compatibility research and additional input processing.
