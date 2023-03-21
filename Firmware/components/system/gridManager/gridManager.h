#define TOTAL_MIDI_NOTES 128
#define MAX_ROWS NUM_OCTAVES * 12


#define VOICE_MSG_STATUS_RANGE_MIN 0x80
#define VOICE_MSG_STATUS_RANGE_MAX 0xEF
#define NUM_QUATERS_IN_WHOLE_NOTE 4
#define NUM_BITS_IN_BYTE 8
#define NUM_SEQUENCER_PHYSICAL_COLUMNS 8
#define NUM_SEQUENCER_PHYSICAL_ROWS 6 

#define MAX_MIDI_VOICE_MSG_DATA_BYTES 2

#define TOTAL_NUM_VIRTUAL_GRID_ROWS 128

#define MIDI_META_MSG 0xFF
#define MIDI_NOTE_OFF_MSG 0x80
#define MIDI_NOTE_ON_MSG 0x90

#define MIDI_NOTE_NUM_IDX 0
#define MIDI_VELOCITY_IDX 1
#define MIDI_MAX_VELOCITY 127


#define MAX_DATA_BYTES 4
#define NUM_OCTAVES 8

typedef struct 
{
    uint16_t gridColumn;
    uint8_t  gridRow;
    uint8_t  statusByte;
    uint8_t  dataBytes[MAX_DATA_BYTES];
    uint8_t  durationInSteps;
    uint8_t  stepsToNext;
} MidiEventParams;


typedef struct GridEventNode GridEventNode;
struct GridEventNode {
    GridEventNode * prevPtr;
    GridEventNode * nextPtr;
    uint32_t rgbColourCode;
    uint32_t deltaTime;
    uint8_t  statusByte;
    uint8_t  dataBytes[MAX_DATA_BYTES];
    uint16_t column;
};


void gridManager_init(void);
void gridManager_updateMidiEventParameters(MidiEventParams eventParams);
MidiEventParams gridManager_getNoteParamsIfCoordinateFallsWithinExistingNoteDuration(uint16_t columnNum, uint8_t rowNum, uint8_t midiChannel);
void gridManager_removeMidiEventFromGrid(MidiEventParams midiEventParams);
void gridManager_addNewMidiEventToGrid(MidiEventParams newEventParams);
void gridManager_midiFileToGrid(uint8_t * midiFileBufferPtr, uint32_t bufferSize);
uint32_t gridManager_gridDataToMidiFile(uint8_t * midiFileBufferPtr, uint32_t bufferSize);
void gridManager_updateGridLEDs(uint8_t rowOffset, uint16_t columnOffset);
void gridManager_printAllLinkedListEventNodesFromBase(uint16_t midiNoteNum);
void gridManager_resetSequencerGrid(uint8_t quantizationSetting);

