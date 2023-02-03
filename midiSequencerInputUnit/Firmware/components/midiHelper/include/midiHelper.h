#define MIDI_SEQUENCER_PPQ 96

#define MIDI_FILE_HEADER_OFFSET 0
#define MIDI_FILE_TRACK_HEADER_OFFSET 14
#define MIDI_FILE_MIDI_EVENTS_OFFSET 22

#define MIDI_FILE_MAX_DELTA_TIME_NUM_BYTES 4

#define MIDI_FILE_TRACK_SIZE_FIELD_NUM_BYTES 4

#define MIDI_FILE_HEADER_NUM_BYTES 4
#define MIDI_TRACK_HEADER_NUM_BYTES 4

#define MIDI_END_OF_TRACK_MSG_NUM_BYTES 3
#define MIDI_TIME_SIG_MSG_NUM_BYTES 3
#define MIDI_SET_TEMPO_MSG_NUM_BYTES 3
#define MIDI_META_MESSAGE_SIZE 3

#define MIDI_FILE_MAX_DELTA_TIME_VALUE 0x0FFFFFFF

#define MIDI_EOF_EVENT_BYTE0 0x00
#define MIDI_EOF_EVENT_BYTE1 0xFF
#define MIDI_EOF_EVENT_BYTE2 0x2F
#define MIDI_EOF_EVENT_BYTE3 0x00

#define MAX_DELTA_TIME_BYTE_VALUE 127

#define MIDI_FILE_MAX_FORMAT_TYPE 2
#define MIDI_FILE_FORMAT_TYPE_OFFSET 9

#define MIDI_FILE_FORMAT_TYPE0 0
#define MIDI_FILE_FORMAT_TYPE1 1
#define MIDI_FILE_FORMAT_TYPE2 2



enum metaEventType
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
};


extern const uint8_t MThd_fileHeaderBytes[MIDI_FILE_HEADER_NUM_BYTES];
extern const uint8_t MTtk_trackHeaderBytes[MIDI_TRACK_HEADER_NUM_BYTES];
extern const uint8_t endOfTrackBytes[MIDI_END_OF_TRACK_MSG_NUM_BYTES];
extern const uint8_t setTimeSignatureMetaEventBytes[MIDI_TIME_SIG_MSG_NUM_BYTES];
extern const uint8_t setTempoMetaEventBytes[MIDI_SET_TEMPO_MSG_NUM_BYTES];


uint8_t generateEmptyMidiFile(uint8_t * filePtr, uint16_t ppq, uint8_t tempo);
int8_t processMidiFileMetaMessage(uint8_t *metaMsgPtr);
uint32_t processMidiFileDeltaTime(uint8_t * midiFilePtr);
uint8_t getDeltaTimeVariableLengthNumBytes(uint32_t deltaTime);
uint8_t getMidiFileFormatType(uint8_t * midiFilePtr);