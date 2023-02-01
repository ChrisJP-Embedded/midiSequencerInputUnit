#include "freertos/queue.h"


#define PSRAM_MIDI_FILE_STORE_SIZE 1024 * 1024 // 1MB (8MB available)

#define MIDI_SEQUENCER_PPQ 96

#define MIDI_FILE_HEADER_OFFSET 0
#define MIDI_FILE_TRACK_HEADER_OFFSET 14
#define MIDI_FILE_MIDI_EVENTS_OFFSET 22

#define MIDI_FILE_TRACK_CHUNK_SIZE_NUM_BYTES 4
#define MIDI_FILE_HEADER_NUM_BYTES 4
#define MIDI_TRACK_HEADER_NUM_BYTES 4
#define MIDI_END_OF_TRACK_MSG_NUM_BYTES 3
#define MIDI_TIME_SIG_MSG_NUM_BYTES 3
#define MIDI_SET_TEMPO_MSG_NUM_BYTES 3

#define MIDI_META_MESSAGE_SIZE 3

extern const uint8_t MThd_fileHeaderBytes[MIDI_FILE_HEADER_NUM_BYTES];     // A midi file ALWAYS starts with these four bytes
extern const uint8_t MTtk_trackHeaderBytes[MIDI_TRACK_HEADER_NUM_BYTES];    // Midi file track data ALWAYS starts with these four bytes
extern const uint8_t endOfTrackBytes[MIDI_END_OF_TRACK_MSG_NUM_BYTES];                // A track chunk is always expected to be terminated with endOfTrackBytes
extern const uint8_t setTimeSignatureMetaEventBytes[MIDI_TIME_SIG_MSG_NUM_BYTES]; // Set time signature preamble, followed by setTimeSignatureMetaEventBytes[2] time signature bytes
extern const uint8_t setTempoMetaEventBytes[MIDI_SET_TEMPO_MSG_NUM_BYTES];         // Set tempo preamble, followed by setTempMetaEventBytes[2] tempo bytes


uint8_t midi_loadMidiFile(uint8_t *const filePtr);
uint32_t midi_getMidiTrackLength(void);
uint8_t generateEmptyMidiFile(uint8_t * filePtr, uint16_t ppq, uint8_t tempo);