# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/synth002/esp/esp-idf-v5.0/components/bootloader/subproject"
  "/home/synth002/Desktop/midiSequencer/midiSequencerInputUnit_Firmware_NimBLEStack/Firmware/build/bootloader"
  "/home/synth002/Desktop/midiSequencer/midiSequencerInputUnit_Firmware_NimBLEStack/Firmware/build/bootloader-prefix"
  "/home/synth002/Desktop/midiSequencer/midiSequencerInputUnit_Firmware_NimBLEStack/Firmware/build/bootloader-prefix/tmp"
  "/home/synth002/Desktop/midiSequencer/midiSequencerInputUnit_Firmware_NimBLEStack/Firmware/build/bootloader-prefix/src/bootloader-stamp"
  "/home/synth002/Desktop/midiSequencer/midiSequencerInputUnit_Firmware_NimBLEStack/Firmware/build/bootloader-prefix/src"
  "/home/synth002/Desktop/midiSequencer/midiSequencerInputUnit_Firmware_NimBLEStack/Firmware/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/synth002/Desktop/midiSequencer/midiSequencerInputUnit_Firmware_NimBLEStack/Firmware/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/synth002/Desktop/midiSequencer/midiSequencerInputUnit_Firmware_NimBLEStack/Firmware/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
