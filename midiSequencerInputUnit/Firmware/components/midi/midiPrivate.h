#include <stdio.h>
#include "driver/uart.h"

#define LOG_TAG "midiIO"
#define MIDI_TX_UART_NUM 1

#define MIDI_HEADER_NUM_BYTES 4
#define MIDI_FILE_SIZE_NUM_BYTES 4
#define MIDI_FILE_FORMAT_NUM_BYTES 2
#define MIDI_FILE_NUMTRACKS_NUM_BYTES 2
#define MIDI_FILE_TIME_DIV_NUM_BYTES 2

#define MIDI_FILE_TRACK_HEADER_NUM_BYTES 4
#define MIDI_FILE_TEMPO_NUM_BYTES 4
#define MIDI_FILE_TRACK_LENGTH_NUM_BYTES 4

#define MIDI_FILE_FIRST_TRACK_OFFSET 14
#define MIDI_FILE_TRACK_TEMPO_OFFSET 4 //RELATIVE TO FIRST TRACK OFFSET

#define MIDI_FILE_HEADER_SIZE_OFFSET 4
#define MIDI_FILE_FORMAT_OFFSET 8
#define MIDI_FILE_NUM_TRACKS_OFFSET 10
#define MIDI_FILE_TIME_DIV_OFFSET 12




typedef enum 
{
    metaEvent_sequenceNum = 0x00,
    metaEvent_textField = 0x01,
    metaEvent_copyright = 0x02,
    metaEvent_trackName = 0x03,
    metaEvent_instrumentName = 0x04,
    metaEvent_lyrics = 0x05,
    metaEvent_marker = 0x06,
    metaEvent_cuePoint = 0x07,
    metaEvent_deviceName = 0x09, //new
    metaEvent_channelPrefix = 0x20,
    metaEvent_midiPort = 0x21, //new
    metaEvent_endOfTrack = 0x2F,
    metaEvent_setTempo = 0x51,
    metaEvent_smpteOffset = 0x54,
    metaEvent_setTimeSig = 0x58,
    metaEvent_keySignature = 0x59,
    metaEvent_sequencerSpecific = 0x7F,
} midiMetaEventType_t;

typedef struct {
    uint32_t headerLength;
    uint8_t  fileFormat;
    uint16_t numTracksInFile;
    uint16_t timeDivision;
} midiFileHeaderData_t;


typedef enum {
    systemMode_edit,
    systemMode_playback
} systemMode_t;


typedef enum {
    midiPlayback_start,
    midiPlayback_stop,
    midiPlayback_setPlaybackPtr,
} midiPlaybackCommand_t;


typedef struct
{
    uint8_t * playbackPtr;
    bool isRunningStatus;
    uint32_t currentDeltaTime;
    uint8_t * fileBaseAddr;
    uint8_t statusByte;
    uint8_t previousStatusForRunningStatus;
    midiFileHeaderData_t midiFileHeaderData;
} midiPlaybackRuntimeData_t;




extern volatile bool deltaTimerFired;
extern QueueHandle_t appToPlaybackTaskQueue;
extern QueueHandle_t playbackTaskToAppQueue;

void initMidiLowLevel(void);
void startDeltaTimer(uint32_t deltaTime);