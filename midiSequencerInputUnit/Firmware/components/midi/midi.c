#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "memory.h"
#include "malloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "include/midi.h"
#include "midiPrivate.h"


static uint8_t processMidiFileHeader(uint8_t *const filePtr);
static uint8_t processMidiFileTrackHeader(uint8_t *const filePtr);
static uint8_t checkMidiFileFormatCompatibility(void);

const uint8_t MThd_fileHeaderBytes[MIDI_FILE_HEADER_NUM_BYTES] = {0x4D, 0x54, 0x68, 0x64};          // A midi file ALWAYS starts with these four bytes
const uint8_t MTtk_trackHeaderBytes[MIDI_TRACK_HEADER_NUM_BYTES] = {0x4D, 0x54, 0x72, 0x6B};        // Midi file track data ALWAYS starts with these four bytes
const uint8_t endOfTrackBytes[MIDI_END_OF_TRACK_MSG_NUM_BYTES] = {0xFF, 0x2F, 0x00};                // A track chunk is always expected to be terminated with endOfTrackBytes
const uint8_t setTimeSignatureMetaEventBytes[MIDI_TIME_SIG_MSG_NUM_BYTES] = {0xFF, 0x51, 0x03};     // Set time signature preamble, followed by setTimeSignatureMetaEventBytes[2] time signature bytes
const uint8_t setTempoMetaEventBytes[MIDI_SET_TEMPO_MSG_NUM_BYTES] = {0xFF, 0x58, 0x04};            // Set tempo preamble, followed by setTempMetaEventBytes[2] tempo bytes

typedef struct 
{
    midiFileHeaderData_t midiFileHeaderData;
    uint32_t trackLength;
} currentMidiFileInfo_t;

currentMidiFileInfo_t midiFileInfo_global;


uint8_t midi_loadMidiFile(uint8_t *const filePtr) 
{
    //This function expects a midi file pointer,
    //which is set to the BASE ADDR of the file.
    //Failure to ahear to this will cause file 
    //to be seen as corrupted.

    //This function attempts to process the midi file and track header sections,
    //saving data into a local global struct 'midiFileInfo_global'.

    //Start by clearing out any previous file data,
    //This struct will be populated as the file is processed
    memset(&midiFileInfo_global, 0, sizeof(midiFileInfo_global));

    uint8_t result = 0;

    result |= processMidiFileHeader(filePtr);
    result |= processMidiFileTrackHeader(filePtr);
    result |= checkMidiFileFormatCompatibility();

    return result; //FAIL IF !=0
}

uint32_t midi_getMidiTrackLength(void)
{
    //The caller must ensure that 'midi_loadMidiFile'
    //has been called prior to this function

    return midiFileInfo_global.trackLength;
}

uint8_t generateEmptyMidiFile(uint8_t * filePtr, uint16_t ppq, uint8_t tempo)
{
    //This function expects a pointer to a file buffer to
    //which a template midi file will be written. The template
    //midi file contains a single END OF TRACK meta event.
    //It returns the number of size of the template in bytes.
    
    //TODO: Add tempo meta message (optional as system default 120bpm)

    if(filePtr == NULL) return 0;

    uint8_t writeAddr = 0;

    //Write midi file header (4 bytes)
    for(uint8_t a = 0; a < sizeof(MThd_fileHeaderBytes); ++a)
    {
        filePtr[writeAddr++] = MThd_fileHeaderBytes[a];
    }

    //Write header length (4 bytes - always equal to 6)
    filePtr[writeAddr++] = 0;
    filePtr[writeAddr++] = 0;
    filePtr[writeAddr++] = 0;
    filePtr[writeAddr++] = 6;

    //Write format (2 bytes - always format 0 - single track format)
    filePtr[writeAddr++] = 0;
    filePtr[writeAddr++] = 0;

    //Write number of tracks that follow (2 bytes - always equal 1 for format 0)
    filePtr[writeAddr++] = 0;
    filePtr[writeAddr++] = 1;

    //Write division (ppq) (2 bytes)
    filePtr[writeAddr++] = (uint8_t)(ppq >> 8);
    filePtr[writeAddr++] = (uint8_t)(ppq);

    //Write midi track header (4 bytes)
    for(uint8_t a = 0; a < sizeof(MTtk_trackHeaderBytes); ++a)
    {
        filePtr[writeAddr++] = MTtk_trackHeaderBytes[a];
    }

    //Write track length in bytes (4 bytes)
    filePtr[writeAddr++] = 0;
    filePtr[writeAddr++] = 0;
    filePtr[writeAddr++] = 0;
    filePtr[writeAddr++] = 4;

    //Write end of track meta message
    //with a delta time of zero
    filePtr[writeAddr++] = 0;
    filePtr[writeAddr++] = 0xFF;
    filePtr[writeAddr++] = 0x2F;
    filePtr[writeAddr++] = 0x00;

    return writeAddr;
}

static uint8_t processMidiFileHeader(uint8_t *const filePtr)
{
    // IMPORTANT: The file pointer MUST be set to FILE BASE before calling this function!!!
    // failure to adhear will cause the midi file to be dismissed as unsupported/corrupt

    uint8_t tempStorage[4];

    // Clear out struct
    memset(&midiFileInfo_global.midiFileHeaderData, 0, sizeof(midiFileInfo_global.midiFileHeaderData)); // size of object being pointed at!

    // Check for midi file header
    for (uint8_t a = 0; a < sizeof(MThd_fileHeaderBytes); ++a)
    {
        if (MThd_fileHeaderBytes[a] != filePtr[a])
        {
            ESP_LOGE(LOG_TAG, "Midi file header not found, aborting midi file processing");
            return 1;
        }
    }

    // Looks like this is a midi file - so read get header data into local cache
    memcpy(&tempStorage, filePtr + MIDI_FILE_HEADER_SIZE_OFFSET, MIDI_FILE_SIZE_NUM_BYTES);
    midiFileInfo_global.midiFileHeaderData.headerLength |= tempStorage[0] << 24;
    midiFileInfo_global.midiFileHeaderData.headerLength |= tempStorage[1] << 16;
    midiFileInfo_global.midiFileHeaderData.headerLength |= tempStorage[2] << 8;
    midiFileInfo_global.midiFileHeaderData.headerLength |= tempStorage[3];

    memcpy(&tempStorage, filePtr + MIDI_FILE_FORMAT_OFFSET, MIDI_FILE_FORMAT_NUM_BYTES);
    midiFileInfo_global.midiFileHeaderData.fileFormat |= tempStorage[0] << 8;
    midiFileInfo_global.midiFileHeaderData.fileFormat |= tempStorage[1];

    memcpy(&tempStorage, filePtr + MIDI_FILE_NUM_TRACKS_OFFSET, MIDI_FILE_NUMTRACKS_NUM_BYTES);
    midiFileInfo_global.midiFileHeaderData.numTracksInFile |= tempStorage[0] << 8;
    midiFileInfo_global.midiFileHeaderData.numTracksInFile |= tempStorage[1];

    memcpy(&tempStorage, filePtr + MIDI_FILE_TIME_DIV_OFFSET, MIDI_FILE_TIME_DIV_NUM_BYTES);
    midiFileInfo_global.midiFileHeaderData.timeDivision |= tempStorage[0] << 8;
    midiFileInfo_global.midiFileHeaderData.timeDivision |= tempStorage[1];

    ESP_LOGI(LOG_TAG, "%ld more header bytes to follow before first track", midiFileInfo_global.midiFileHeaderData.headerLength);
    ESP_LOGI(LOG_TAG, "Midi file format: %d", midiFileInfo_global.midiFileHeaderData.fileFormat);
    ESP_LOGI(LOG_TAG, "Num tracks in midi file: %d", midiFileInfo_global.midiFileHeaderData.numTracksInFile);
    ESP_LOGI(LOG_TAG, "timeDivision value: %d", midiFileInfo_global.midiFileHeaderData.timeDivision);

    return 0;
}



static uint8_t processMidiFileTrackHeader(uint8_t *const filePtr)
{
    uint8_t tempStorage[4];

    // Check for midi file track header
    for (uint32_t a = 0; a < MIDI_FILE_TRACK_HEADER_NUM_BYTES; ++a)
    {
        ESP_LOGI(LOG_TAG, "File[%ld]=%0x", a, filePtr[MIDI_FILE_FIRST_TRACK_OFFSET + a]);
        if (MTtk_trackHeaderBytes[a] != filePtr[MIDI_FILE_FIRST_TRACK_OFFSET + a])
        {
            ESP_LOGE(LOG_TAG, "Midi file track header not found, aborting midi file processing");
            return 1;
        }
    }

    memcpy(&tempStorage, filePtr + MIDI_FILE_FIRST_TRACK_OFFSET + MIDI_FILE_TRACK_HEADER_NUM_BYTES, MIDI_FILE_TRACK_LENGTH_NUM_BYTES);
    midiFileInfo_global.trackLength |= tempStorage[0] << 24;
    midiFileInfo_global.trackLength |= tempStorage[1] << 16;
    midiFileInfo_global.trackLength |= tempStorage[2] << 8;
    midiFileInfo_global.trackLength |= tempStorage[3];

    ESP_LOGI(LOG_TAG, "Current track length in bytes: %ld", midiFileInfo_global.trackLength);

    return 0;
}


static uint8_t checkMidiFileFormatCompatibility(void)
{
    //Determine the FORMAT of the MIDI
    //file and take appropriate action.
    switch(midiFileInfo_global.midiFileHeaderData.fileFormat)
    {
        case 0: //---FULL SUPPORT---
            //This file type contains a sequence
            //made up of a single track chunk
            ESP_LOGI(LOG_TAG, "Format 0 MIDI file detected - File contains single track");
            break;

        case 1:  //---NOT SUPPORTED---
            ESP_LOGE(LOG_TAG, "ERROR: Format 1 MIDI file detected - File has multiple track chunks - UNSUPPORTED");
            return 1;
            break;

        case 2: //---NOT SUPPORTED---
            ESP_LOGE(LOG_TAG, "ERROR: Format 2 MIDI file detected - File has multiple track chunks - UNSUPPORTED");
            return 1;
            break;
    }

    return 0;
}