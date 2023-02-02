#include "esp_log.h"
#include "include/system.h"
#include "include/systemEvents.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "guiMenu.h"
#include "rotaryEncoders.h"
#include "bleCentClient.h"
#include "switchMatrix.h"
#include "ipsDisplay.h"
#include "fileSys.h"
#include "midiHelper.h"
#include "sequencerGrid.h"

#define LOG_TAG "systemEvents"

//--- Public 
uint8_t createNewSequencerProject(uint8_t * midiFileBufferPtr, char * fileName, uint8_t tempo, uint8_t quantization)
{
    uint8_t ret = 0;
    uint8_t size;

    if(midiFileBufferPtr == NULL) return 1;

    size = generateEmptyMidiFile(midiFileBufferPtr, MIDI_SEQUENCER_PPQ, tempo);

    if(size == 0) return 1;

    //midiFileDataSize = size;

    ret = fileSys_writeFile(fileName, midiFileBufferPtr, size, true); //write midi file template to file

    return ret;
}


//--- Public
uint8_t deleteFile(char * fileName)
{
    if(fileName == NULL) return 1;
    return fileSys_deleteFile(fileName);
}


//--- Public
uint8_t printEntireMidiFileContentToConsole(uint8_t * fileBASE, uint32_t fileSize)
{
    //This function expects base pointer to a properly formatted 
    //midi file - the entire conent will be printed

    for(uint32_t a = 0; a < fileSize; ++a)
    {
        ESP_LOGI(LOG_TAG, "FILE[%ld]: %0x", a, *fileBASE);
        ++fileBASE;
    }

    return 0;
}