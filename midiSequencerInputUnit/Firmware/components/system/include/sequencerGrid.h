
#include "ledDrivers.h"

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







void addNewMidiEventToGrid(uint16_t columnNum, uint8_t statusByte, uint8_t midiNoteNumber, uint8_t midiVelocity, uint8_t durationInSteps, bool autoAddNoteOff);
void midiFileToGrid(uint8_t * midiFileBufferPtr, uint32_t bufferSize);
uint32_t gridDataToMidiFile(uint8_t * midiFileBufferPtr, uint32_t bufferSize);
void updateGridLEDs(uint8_t rowOffset, uint16_t columnOffset);
void printAllLinkedListEventNodesFromBase(uint16_t midiNoteNum);
void resetSequencerGrid(uint8_t ppqn, uint8_t quantization);
