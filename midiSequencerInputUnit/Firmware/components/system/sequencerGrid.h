
#include "ledDrivers.h"

#define TOTAL_MIDI_NOTES 128
#define MAX_ROWS NUM_OCTAVES * 12


#define VOICE_MSG_STATUS_RANGE_MIN 0x80
#define VOICE_MSG_STATUS_RANGE_MAX 0xEF
#define NUM_QUATERS_IN_WHOLE_NOTE 4
#define NUM_BITS_IN_BYTE 8
#define NUM_GRID_COLUMNS_PER_ROW 8
#define NUM_SEQUENCER_HARDWARE_ROWS 6 
#define MAX_DELTA_TIME_BYTE_LENGTH 4
#define MAX_MIDI_VOICE_MSG_DATA_BYTES 2
#define MAX_DELTA_TIME_BYTE_VALUE 127

#define MIDI_META_MSG 0xFF
#define MIDI_NOTE_OFF_MSG 0x80
#define MIDI_NOTE_ON_MSG 0x90

#define MIDI_EOF_EVENT_BYTE0 0x00
#define MIDI_EOF_EVENT_BYTE1 0xFF
#define MIDI_EOF_EVENT_BYTE2 0x2F
#define MIDI_EOF_EVENT_BYTE3 0x00

#define MAX_DATA_BYTES 4
#define NUM_OCTAVES 8


typedef struct SequencerGridItem_t SequencerGridItem_t;
struct SequencerGridItem_t {
    SequencerGridItem_t * prevPtr;
    SequencerGridItem_t * nextPtr;
    rgbLedColour_t rgbColourCode;
    uint32_t deltaTime;
    uint8_t  statusByte;
    uint8_t  dataBytes[MAX_DATA_BYTES];
    uint16_t column;
};


typedef struct {
    uint16_t totalGridColumns;
    uint32_t midiDataNumBytes;
    uint8_t sequencerPPQN;
    uint8_t projectQuantization;
    SequencerGridItem_t * gridLinkedListTailPtrs[TOTAL_MIDI_NOTES];
    SequencerGridItem_t * gridLinkedListHeadPtrs[TOTAL_MIDI_NOTES];
} SequencerGridData_t;



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



uint8_t addNewNoteToGrid(uint16_t columnNum, uint8_t statusByte, uint8_t midiNoteNumber, uint8_t midiVelocity, uint8_t durationInSteps, bool autoAddNoteOff);
uint8_t midiFileToGrid(uint8_t * midiFileDataPtr);
uint32_t gridDataToMidiFile(uint8_t * fileBuffer);
void updateGridLEDs(uint8_t rowOffset, uint16_t columnOffset);
void printAllLinkedListEventNodesFromBase(uint16_t midiNoteNum);
void resetSequencerGrid(uint8_t ppqn, uint8_t quantization);
