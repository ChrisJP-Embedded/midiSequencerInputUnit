#include <stdbool.h>
#include <stdio.h>


uint8_t createNewSequencerProject(uint8_t * midiFileBufferPtr, char * fileName, uint8_t tempo, uint8_t quantization);
uint8_t printEntireMidiFileContentToConsole(uint8_t * fileBASE, uint32_t fileSize);