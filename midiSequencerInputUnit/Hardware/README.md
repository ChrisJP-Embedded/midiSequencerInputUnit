# This is the Kicad 6 project for the 6 x 8 sequencer input unit.

**Summary**

NOTE: This is an initial prototype.

The sequencer grid is made up of a 6x8 switch matrix (using generic CherryMX style keyboard switches).

The inputs of each COLUMN of switches are connected to a common driving line. The outputs of ROW are connected to a common row scan/input line (via signal diode).
The MCU clock a decade counter which drives the columns sequentially. Each row output passes through a schmitt trigger to the MCU for reading.

An I2C bus is used to control four TI LP5862 RGB LED drivers, which provide one RGB LED per switch.
The I2C bus trace widths are designed to keep the capacitance of the I2C bus within required spec.

Two rotary encoders (each with integrated SPST switch) provide additional UI functionality.
LPFs are present on encoder phase and switch outputs in order to provide some HW debouncing.

A small form factor IPS display is driven via SPI bus.

An ESP32S3 MCU controls the unit. USB port provides programming, console, and JTAG interface.
USB traces designed for 90ohm differential impedance.

A few 2N7002 logic-level mosfets are used as switches throughout.


![Model](https://github.com/ChrisJP-Embedded/midiSequencer/blob/main/images/inputUnitPCB.png)
