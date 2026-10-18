- Use PySerial to handle USB serial communication between PC and arduino
- PySide6 to create windows application with button, numerical graphs and indicators
- Some arduino firmware that can read peripherals and transmits data

- Python for better data design/visuals or C++ for better data code
  - Which for this project?

- Arduino Nano does not natively support USB HID (human interface device)
  - Need to use serial communication?
    - Sends sensor data, button presses, etc. (arduino)
    - Computer opens COM port, “listens” for incoming data, then reacts with actions
- For serial-to-joystick we can make an emulator (in UnoJoy)
  - Emulates USB-HID
- ATmega32U4 has a built in USB interface so that the Leonardo and Micro can act as a mouse, keyboard, or joystick
- ARmega328P USB connector is implemented by a separate bridge chip
  - Present themselves to the system as a virtual-serial port (USB-Serial) not an HID
