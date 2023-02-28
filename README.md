# Monophonic 128-Step Sequencer

This is an ongoing project which implements a BLE enabled hardware sequencer - a tool for aiding musical composition for midi (Musical Instrument Digital Interface) controlled devices such as synthesizers and drum machines. The project is based around the ESP32S3-WROOM-1 module, a low-cost wireless enabled module featuring a dual-core ESP32S3 MCU, as well as on-module flash and PSRAM.

This project features both custom firmware (written in C) and custom hardware design (all part selection, schematic and PCB design) using KiCAD 6. 


The sequencer input unit features an RGB lit 6 x 8 switch matrix which makes up the sequencer input grid, as well as a small form factor IPS display and rotary encoders in order to provide a GUI interface. The sequencer input unit communicates wirelessly (via Bluetooth low-energy) to a base unit which provides a wireless interface to the MIDI physical layer.

**Note: First stage prototype**
![Model](https://github.com/ChrisJP-Embedded/midiSequencer/blob/main/images/inputUnitPCB.png)










