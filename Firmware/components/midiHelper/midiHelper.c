#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "genericMacros.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "midiHelper.h"

#define LOG_TAG "MidiHelper"

const uint8_t MThd_fileHeaderBytes[MIDI_FILE_HEADER_NUM_BYTES]   = {0x4D, 0x54, 0x68, 0x64}; //A midi file ALWAYS starts with these four bytes
const uint8_t MTtk_trackHeaderBytes[MIDI_TRACK_HEADER_NUM_BYTES] = {0x4D, 0x54, 0x72, 0x6B}; //Midi file track data ALWAYS starts with these four bytes
const uint8_t endOfTrackBytes[MIDI_END_OF_TRACK_MSG_NUM_BYTES]   = {0xFF, 0x2F, 0x00}; //A track chunk is always terminated with endOfTrackBytes

 void generateMidiFileTemplate(uint8_t * filePtr, uint16_t ppq, uint8_t tempo)
{
    //This function expects a pointer to a file buffer,
    //it will write a default midi file template to the
    //base index of the file pointer.

    assert(filePtr != NULL);
    uint8_t writeAddr = 0;

    //Write midi file header chunk 'MThd' identifier
    for(uint8_t a = 0; a < sizeof(MThd_fileHeaderBytes); ++a)
    {
        filePtr[writeAddr++] = MThd_fileHeaderBytes[a];
    }
  
    //Header length field (always fixed)
    filePtr[writeAddr++] = MIDI_FILE_HEADER_LENGTH_BYTE0;
    filePtr[writeAddr++] = MIDI_FILE_HEADER_LENGTH_BYTE1;
    filePtr[writeAddr++] = MIDI_FILE_HEADER_LENGTH_BYTE2;
    filePtr[writeAddr++] = MIDI_FILE_HEADER_LENGTH_BYTE3;

    //Header file format field (sequencer only supports format 0, single track)
    filePtr[writeAddr++] = MIDI_FILE_HEADER_FILE_FORMAT0_BYTE0;
    filePtr[writeAddr++] = MIDI_FILE_HEADER_FILE_FORMAT0_BYTE1;

    //Header number of tracks field (always a single track for format 0)
    filePtr[writeAddr++] = MIDI_FILE_NUM_TRACKS_BYTE0;
    filePtr[writeAddr++] = MIDI_FILE_NUM_TRACKS_BYTE1;

    //Header division field (ppq - pulses per quater note)
    filePtr[writeAddr++] = (uint8_t)(ppq >> NUM_BITS_IN_BYTE);
    filePtr[writeAddr++] = (uint8_t)(ppq);

    //Write midi FILE track chunk identifier 'MTtk'
    for(uint8_t a = 0; a < sizeof(MTtk_trackHeaderBytes); ++a)
    {
        filePtr[writeAddr++] = MTtk_trackHeaderBytes[a];
    }
}


uint8_t getDeltaTimeVariableLengthNumBytes(uint32_t deltaTime)
{
    //This function takes in a 32-bit deltaTime value
    //and returns the number of bytes required to 
    //represent it in when converted to a variable length
    //value (as delta times appear in a midi file)
    
    assert(deltaTime <= MIDI_FILE_MAX_DELTA_TIME_VALUE);

    uint8_t count = 1; //Delta-time min one byte
    while(deltaTime >>= NUM_BITS_IN_BYTE) count++;
    return count;
}


uint32_t processMidiFileDeltaTime(uint8_t * midiFilePtr)
{
    //This function processes a midi file delta time,
    //it expects a pointer to the base of a delta time

    //This function is called when converting an
    //existing midi file to grid data.

    //A delta-time is variable length value, between 1 and 4 
    //bytes in length, it specifies (in midi ticks) the time
    //that must pass before the event it is assosiated with
    //should be transmitted on the physical midi layer.

    //Delta-time bytes appear in MSB first order, the MSBIT
    //in each byte is a flag which indicates that there are
    //more delta-time bytes to follow when SET.
    //To generate the reconstructed value, the MSBIT of each
    //delta-time byte is removed and then remaining 7 bits of
    //each byte are concatenated.

    //RETURNS: The reconstructed 32-bit (max) delta-time value.

    assert(midiFilePtr != NULL);

    uint32_t deltaTimeResult = 0;
    uint8_t bytesProcessed = 0;

    do
    {
        if(bytesProcessed > (MIDI_FILE_MAX_DELTA_TIME_NUM_BYTES - 1)) assert(0);
        deltaTimeResult <<= (NUM_BITS_IN_BYTE - 1);
        deltaTimeResult |= *midiFilePtr & 0x7F;
        bytesProcessed++;

    }while(*midiFilePtr++ & (1 << (NUM_BITS_IN_BYTE - 1)));

    return deltaTimeResult;
}


uint8_t getMidiFileFormatType(uint8_t * midiFilePtr)
{
    //This function checks the midi file format type,
    //it expects a pointer to the BASE of a midi file

    //There midi file format type is specified within
    //the file header data chunk, represented by two
    //bytes at offset 8 from zero file BASE.
    //Only the LSB is currently utilized, which is
    //at byte offset 9 from zero file BASE.

    //There are three types of midi file format:
    //TYPE 0: The file contains a single multi-channel track chunk.
    //TYPE 1: The file has multiple tracks chunks to be processed simultaneously.
    //TYPE 2: The file contains multiple independent tracks.

    assert(midiFilePtr != NULL);

    //Confirm the file header byte signature before continuing 
    for(uint8_t a = 0; a < sizeof(MThd_fileHeaderBytes); ++a)
    {
        if(midiFilePtr[a] != MThd_fileHeaderBytes[a])
        { assert(0); }
    }

    uint8_t midiFileFormatType = *(midiFilePtr + MIDI_FILE_FORMAT_TYPE_OFFSET);

    switch(midiFileFormatType)
    {
        case MIDI_FILE_FORMAT_TYPE0: //---FULL SUPPORT ---//
            ESP_LOGI(LOG_TAG, "Format type 0 midi file detected");
            break;

        case MIDI_FILE_FORMAT_TYPE1:  //---NOT SUPPORTED ---//
            ESP_LOGE(LOG_TAG, "ERROR: Format 1 midi file detected - UNSUPPORTED");
            break;

        case MIDI_FILE_FORMAT_TYPE2: //---NOT SUPPORTED ---//
            ESP_LOGE(LOG_TAG, "ERROR: Format 2 midi file detected - UNSUPPORTED");
            break;

        default:
            assert(0);
            break;
    }

    return midiFileFormatType;
}



int8_t processMidiFileMetaMessage(uint8_t * midiFilePtr)
{
    //NOT FULLY IMPLEMENTED, CURRENTLY ONLY EXPECTS
    //META MESSAGES WHICH ARE A SINGLE BYTE IN LENGTH
    //BARE BONES TO GET SYSTEM GOING.

    //A meta message has the follow byte structure:
    //meta_message[0] = 0xFF (indicates meta message)  <- *midiFilePtr
    //meta_message[1] = meta status byte
    //meta_message[2] = length byte (number of bytes that follow)
    
    //RETURNS:

    //ON SUCCESS: the single byte length of the meta message

    //ON FAILURE: if the meta message is not recognized the
    //return value will be -1, indicating corrupt data.

    assert(midiFilePtr != NULL);
    assert(*midiFilePtr == 0xFF);

    uint8_t metaMsgLengthInBytes;
    uint8_t metaStatusByte;

    midiFilePtr++;
    metaStatusByte = *midiFilePtr;
    midiFilePtr++; 
    metaMsgLengthInBytes = *midiFilePtr;


    //Check meta message status byte
    switch (metaStatusByte)
    {
        case metaEvent_deviceName:
            ESP_LOGI(LOG_TAG, "metaEvent_deviceName detected");
            break;

        case metaEvent_midiPort:
            ESP_LOGI(LOG_TAG, "metaEvent_midiPort detected");
            break;

        case metaEvent_sequenceNum:
            //Two data bytes expected
            ESP_LOGI(LOG_TAG, "metaEvent_sequenceNum detected");
            break;

        case metaEvent_cuePoint:
        case metaEvent_marker:
        case metaEvent_lyrics:
        case metaEvent_instrumentName:
        case metaEvent_trackName:
        case metaEvent_copyright:
        case metaEvent_textField:
            //All meta events of variable length, not yet supported
            ESP_LOGI(LOG_TAG, "Ignored variable-length meta message");
            break;

        case metaEvent_channelPrefix:
            //Single data byte expected
            ESP_LOGI(LOG_TAG, "metaEvent_channelPrefix detected");
            break;

        case metaEvent_endOfTrack:
            //Single byte
            ESP_LOGI(LOG_TAG, "metaEvent_endOfTrack detected");
            break;

        case metaEvent_setTempo:
            //Three data bytes expected
            ESP_LOGI(LOG_TAG, "metaEvent_setTempo detected");
            break;

        case metaEvent_smpteOffset:
            //Five data bytes expected
            ESP_LOGI(LOG_TAG, "metaEvent_smpteOffset detected (not supported)");
            break;

        case metaEvent_setTimeSig:
            //Four data bytes expected
            ESP_LOGI(LOG_TAG, "metaEvent_setTimeSig detected");
            break;

        case metaEvent_keySignature:
            //Two data bytes expected
            ESP_LOGI(LOG_TAG, "metaEvent_keySignature detected");
            break;

        case metaEvent_sequencerSpecific:
            //Custom meta message - device specific
            //Variable length
            ESP_LOGE(LOG_TAG, "metaEvent_sequencerSpecific detected");
            break;

        default:
            ESP_LOGE(LOG_TAG, "Error: Unrecognized meta message status byte");
            return -1; //Corrupt data detected
            break;
    }

    return metaMsgLengthInBytes;
}