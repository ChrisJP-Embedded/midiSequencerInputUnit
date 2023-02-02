#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "midiHelper.h"

#define LOG_TAG "MidiHelper"
#define NUM_BITS_IN_BYTE 8
#define GET_MSBIT_IN_BYTE(x)    (x & 0x80)

const uint8_t MThd_fileHeaderBytes[MIDI_FILE_HEADER_NUM_BYTES] = {0x4D, 0x54, 0x68, 0x64};          // A midi file ALWAYS starts with these four bytes
const uint8_t MTtk_trackHeaderBytes[MIDI_TRACK_HEADER_NUM_BYTES] = {0x4D, 0x54, 0x72, 0x6B};        // Midi file track data ALWAYS starts with these four bytes
const uint8_t endOfTrackBytes[MIDI_END_OF_TRACK_MSG_NUM_BYTES] = {0xFF, 0x2F, 0x00};                // A track chunk is always expected to be terminated with endOfTrackBytes
const uint8_t setTimeSignatureMetaEventBytes[MIDI_TIME_SIG_MSG_NUM_BYTES] = {0xFF, 0x51, 0x03};     // Set time signature preamble, followed by setTimeSignatureMetaEventBytes[2] time signature bytes
const uint8_t setTempoMetaEventBytes[MIDI_SET_TEMPO_MSG_NUM_BYTES] = {0xFF, 0x58, 0x04};            // Set tempo preamble, followed by setTempMetaEventBytes[2] tempo bytes


uint8_t generateEmptyMidiFile(uint8_t * filePtr, uint16_t ppq, uint8_t tempo)
{
    //This function expects a pointer to a file buffer,
    //it will write a default midi file template with a
    //single track that contains a single midi event,
    //an EOF meta event.
    
    //TODO: Add tempo meta message (currentlY)

    if(filePtr == NULL) return 0;

    uint8_t writeAddr = 0;

    //Write midi file header (4 bytes)
    for(uint8_t a = 0; a < MIDI_FILE_HEADER_NUM_BYTES; ++a)
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

    return writeAddr;
}


uint8_t getDeltaTimeVaraibleLengthNumBytes(uint32_t deltaTime)
{
    //This function takes in a 32-bit deltaTime value
    //and returns the number of bytes required to 
    //represent it in when converted to a variable length
    //value (as a delta times appear in a midi file)
    
    assert(deltaTime <= MIDI_FILE_MAX_DELTA_TIME_VALUE);

    //Delta-time min one byte
    unsigned char count = 1;
    while(deltaTime >>= (NUM_BITS_IN_BYTE - 1))
    {
        ++count;
    } 
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
    //should be transmitted.

    //Delta-time bytes appear in MSB first order, the MSBIT
    //in each byte is a flag which indicates that there are
    //more delta-time bytes to follow when SET.
    //To generate the reconstructed value, the MSBIT of each
    //delta-time byte is removed and then remaining 7 bits of
    //each byte are concatenated.

    //RETURNS: The reconstructed 32-bit (max) delta-time value.

    assert(midiFilePtr != NULL);

    uint32_t rawDeltaTimeBuffer = 0;
    uint32_t finaDeltaTimeResult = 0;
    uint8_t zeroBaseByteCount = 0;
    bool stillHasBytesToRead = true;

    while(stillHasBytesToRead)
    {
        stillHasBytesToRead = false; //May only have a single byte
        rawDeltaTimeBuffer |= *midiFilePtr; //Get first delta-time byte

        //If MSBIT is SET then delta-time has more bytes
        if(GET_MSBIT_IN_BYTE(*midiFilePtr)) 
        {
            //We get here if bit 8 of the current delta-time byte
            //is SET (meaning at least one more delta-time byte)

            //Delta-times are FOUR BYTES max in length
            if(zeroBaseByteCount < MIDI_FILE_MAX_DELTA_TIME_NUM_BYTES) 
            {
                rawDeltaTimeBuffer <<= NUM_BITS_IN_BYTE;
                ++zeroBaseByteCount;
                midiFilePtr += 1;
                stillHasBytesToRead = true;
            }
            else
            {
                //The final byte of a midi file
                //delta-time should always have 
                //its MSBIT CLEAR.
                //If that bit is SET then we're
                //dealing with a system fault.
                assert(0);
            }
        }
    }

    //Finished reading all midi file format delta-time bytes, 
    //we now need to construct the final concatenated result
    for (uint8_t a = 0; a <= zeroBaseByteCount; ++a)
    {
        //For each delta-time byte we need to remove bit 8 and concatenate the result
        finaDeltaTimeResult |= ((rawDeltaTimeBuffer & (0x0000007F << (a * NUM_BITS_IN_BYTE))) >> ((a ? 1 : 0) * a));
    }

    return finaDeltaTimeResult;
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

    //RETURNS: An unsigned byte with value of either 0, 1, 2.

    assert(midiFilePtr != NULL);

    //Confirm the file header byte signature before continuing 
    for(uint8_t a = 0; a < MIDI_FILE_HEADER_NUM_BYTES; ++a)
    {
        if(midiFilePtr[a] != MThd_fileHeaderBytes[a])
        { assert(0); }
    }

    uint8_t midiFileFormatType = *(midiFilePtr + MIDI_FILE_FORMAT_TYPE_OFFSET);

    assert(midiFileFormatType <= MIDI_FILE_MAX_FORMAT_TYPE);

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
    }

    return midiFileFormatType;
}



int8_t processMidiFileMetaMessage(uint8_t * metaMsgPtr)
{
    //This function processes a midi file meta message,
    //it expects a pointer to the base of a meta message

    //This function is called when converting 
    //an existing midi file to grid data. 

    //A meta message has the follow byte structure:
    //meta_message[0] = 0xFF (indicates meta message)  <- *metaMsgPtr
    //meta_message[1] = meta status byte
    //meta_message[2] = length byte (number of bytes that follow)
    
    //RETURNS:

    //ON SUCCESS: the length of the meta message (an EOF meta
    //message is the only meta message with a length of zero)

    //ON FAILURE: if the meta message is not recognized the
    //return value will be -1, indicating corrupt data.

    assert(metaMsgPtr != NULL);
    assert(*metaMsgPtr == 0xFF);

    uint8_t metaMsgLengthInBytes;
    uint8_t metaStatusByte;

    metaMsgPtr += 1;
    metaStatusByte = *metaMsgPtr;
    metaMsgPtr += 1; 
    metaMsgLengthInBytes = *metaMsgPtr;


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
            //All meta events of variable length
            //Not bothered about any of these so just increment file pointer
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
            //Custom meta messages for the sequencer
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