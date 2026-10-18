## getting started

currently workign with raspberry pi pico w with circuitpython

added essential libraries to lib on controller, download them from these links
[essential](https://github.com/adafruit/Adafruit_CircuitPython_Bundle/releases/download/20261016/adafruit-circuitpython-bundle-10.x-mpy-20261016.zip)
    - DO NOT INSTALL ESSENTIAL UNLESS THERE ARE ISSUES RELATING TO DEPENDENCIES, EVEN THEN, ONLY INSTALL WHAT IS ABSOLUTELY NECESSARY (there is only 1 mb of flash memory)
[hid stuff](https://github.com/adafruit/Adafruit_CircuitPython_HID/releases/download/6.1.10/adafruit-circuitpython-hid-10.x-mpy-6.1.10.zip)
    - this is what actually matters, place the library contents into the lib of the pico w device

## things to note

 - remember that in order for files to run on the pico, they need to physically be on the pico titled code.py
 - the pico outputs 3.3volts, most of the peripherals require 5volts
 - the pico only has three analog ports, so we may need to look into alternative solutions
    - that being said, it would be entirely possible to just make our own adc, it would make for a good challenge

![collaboration.md]
