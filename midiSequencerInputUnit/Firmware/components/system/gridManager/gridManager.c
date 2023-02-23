#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "memory.h"
#include "malloc.h"
#include "genericMacros.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "ledDrivers.h"
#include "esp_task_wdt.h"
#include "midiHelper.h"
#include "genericDLL/genericDLL.h"
#include "gridManager.h"

#define LOG_TAG "sequencerGrid"
#define TEMPO_IN_MICRO 500000
#define HAS_MORE_DELTA_TIME_BYTES(X) ((0x80 & X) && (1 << 8))
#define PULSES_PER_QUATER_NOTE 96 //Pulses per quater note

//Each row represents one of the possible 128 midi notes,
//Each column of the sequencer represents a unit of step-time

//Step-time is the smallest unit of time (note duration) possible (expressed in 'midi ticks')
//for the current project, and is given by: ((sequencer_ppqn*4) / sequencer_quantization)

//The midi tick rate is determined by: 60000 / (BPM * PPQN) (in milliseconds)

struct {
    uint16_t totalGridColumns;
    uint32_t midiDataNumBytes;
    uint8_t sequencerPPQN;
    uint8_t projectQuantization;
    GridEventNode_t * gridLinkedListTailPtrs[TOTAL_MIDI_NOTES];
    GridEventNode_t * gridLinkedListHeadPtrs[TOTAL_MIDI_NOTES];
}  g_GridData;


static GridEventNode_t * getPointerToCorespondingNoteOffEventNode(GridEventNode_t * nodePtr);
static GridEventNode_t * getPointerToNextNoteOnEventInListIfOneExists(GridEventNode_t * noteOnEventPtr);
static GridEventNode_t * getPointerToEventNodeIfExists(uint8_t targetStatusByte, uint8_t rowNum, uint16_t columnNum);
static void addCorrespondingNoteOff(GridEventNode_t * noteOnNode, uint16_t noteDuration);
static void generateDeltaTimesForCurrentGrid(void);
static void freeAllGridData(void);


//---- Public Interface
void resetSequencerGrid(uint8_t quantizationSetting)
{
    //Should be called at startup and immediately
    //before loading any new projects via 'midiFileToGrid'
    g_GridData.projectQuantization = quantizationSetting;
    g_GridData.sequencerPPQN = PULSES_PER_QUATER_NOTE;
    //Completely clear the current 
    //grid data cache, automatically
    //frees all nodes/events if any exist
    freeAllGridData();
    //The grid is now cleared and ready
    //for new nodes/events to be added
}


//---- Public Interface
void addNewMidiEventToGrid(MidiEventParams_t newEventParams)
{
    //This public function is called by the host when 
    //a new event needs to be added to the virtual grid. 

    //The caller is expected to have checked for any 
    //event nodes at the target coordinate with a matching
    //statusByte. Duplicate event types at the same virtual
    //grid coordinate are NOT ALLOWED.

    //The caller is also expected to have checked that the
    //target coordinate does NOT fall within the duration 
    //of an existing note IF the event to be added is a note-on.

    assert(newEventParams.gridRow < TOTAL_MIDI_NOTES);

    GridEventNode_t * newNodePtr = NULL;
    GridEventNode_t * tempNodePtr = NULL;
    bool autoAddNoteOff = false;

    //Its possible for multiple event nodes may share the same virtual grid cooridinate,
    //BUT - each event node at that coordinate MUST ALWAYS have a unqiue statusByte.
    //Its the callers responsibility to check that there are no existing event nodes with
    //the same statusByte at the target coordinate, so this is considered a system fault.
    if(getPointerToEventNodeIfExists(newEventParams.statusByte, newEventParams.gridColumn, newEventParams.gridRow) != NULL) assert(0);

    if((CLEAR_LOWER_NIBBLE(newEventParams.statusByte) == MIDI_NOTE_ON_MSG) && (newEventParams.durationInSteps > 0))
    {
        //IMPORTANT: When a note is added with durationInSteps == 0,
        //its a special case used to indicate that a corresponding
        //note-off event should note be automatically added.
        //This is a requirement when performing midi file to grid
        //data conversions, where ALL events are added manually.

        //Quick check to make sure the note number is within range
        assert(newEventParams.dataBytes[MIDI_NOTE_NUM_IDX] < TOTAL_MIDI_NOTES);

        //Since this is a note-on event the system requires that a
        //corresponding note-off event is generated automatically
        autoAddNoteOff = true;
    }


    //Create the new event node
    newNodePtr = genericDLL_createNewNode();
    assert(newNodePtr != NULL);
    //Assign parameters to new midi event node
    //TODO: WHEN MORE EVENT TYPES SUPPORTED,
    //ADD SWITCH HERE THAT WILL RANGE CHECK
    //THE MESSAGE DATA BYTES.
    newNodePtr->column = newEventParams.gridColumn;
    newNodePtr->statusByte = newEventParams.statusByte;
    //newNodePtr->durationInSteps = newEventParams.durationInSteps;
    memcpy(&newNodePtr->dataBytes, &newEventParams.dataBytes, MAX_DATA_BYTES);
    //TODO: ADD RGB COLOUR CODE ASSIGNMENT - CURRENTLY HARDCODED


    if((g_GridData.gridLinkedListHeadPtrs[newEventParams.gridRow] != NULL) && 
        (newEventParams.gridColumn < g_GridData.gridLinkedListTailPtrs[newEventParams.gridRow]->column))
    {
        //If we get here then the linked list for the target row has existing event
        //nodes allocated AND the target column is LESS than the last event node in
        //the list. Therefore, we need to perform a list INSERTION operaton for the
        //new event node we want to add.

        //If a NULL pointer found here we have a system fault
        assert(g_GridData.gridLinkedListTailPtrs[newEventParams.gridRow] != NULL);

        if(CLEAR_LOWER_NIBBLE(newEventParams.statusByte) == MIDI_NOTE_ON_MSG)
        {
            //As note events can have a duration of multiple sequencer steps,
            //we need to ALWAYS make sure that we're not placing a new note-on
            //with the duration of an existing note.
            //Its the callers responsibility to have checked that the target
            //coordinate does not fall within the duration of an existing note
            //event, so this is considered a system fault.
            MidiEventParams_t params = getNoteParamsIfCoordinateFallsWithinExistingNoteDuration(newEventParams.gridColumn, newEventParams.gridRow, 0);
            if(params.statusByte != 0) assert(0);
        }

        tempNodePtr = g_GridData.gridLinkedListTailPtrs[newEventParams.gridRow];

        //We need to identify the node at or immediately previous
        //to the insertion coordinate within the virtual grid.
        //IMPORTANT:
        //If there is no such node then set tempNodePtr to NULL,
        //in order for 'genericDLL_insertNewNodeIntoLinkedList' 
        //to detect that case and insert the node at list head.
        while(tempNodePtr != NULL)
        {
            if(tempNodePtr->column <= newEventParams.gridColumn) break;
            if(tempNodePtr->prevPtr == NULL)
            {
                tempNodePtr = NULL;
                break;
            } 
            tempNodePtr = tempNodePtr->prevPtr;
        }

        //IMPORTANT:
        //IF(tempNodePtr != NULL) The new event node will be inserted at tempNodePtr->nextPtr
        //IF(tempNodePtr == NULL) The new event node will be inserted as the new HEAD of the list.
        genericDLL_insertNewNodeIntoLinkedList(newNodePtr, tempNodePtr, &g_GridData.gridLinkedListHeadPtrs[newEventParams.gridRow]);
    }
    else
    {
        //If we get here, either the target rows linked list has no existing
        //event nodes OR the target column is equal to or greater than the last
        //node currently in the list. Therefore, we need to perform an APPEND
        //operation for the new node we want to add.

        //Append new event node to the linked list
        genericDLL_appendNewNodeOntoLinkedList(newNodePtr, &g_GridData.gridLinkedListHeadPtrs[newEventParams.gridRow], 
                                               &g_GridData.gridLinkedListTailPtrs[newEventParams.gridRow]);

        //Update a record of the total columns in the project if required.
        if(newEventParams.gridColumn > g_GridData.totalGridColumns) g_GridData.totalGridColumns = newEventParams.gridColumn;
    }

    if(autoAddNoteOff) addCorrespondingNoteOff(newNodePtr, newEventParams.durationInSteps);
}


//---- Public Interface
void removeMidiEventFromGrid(MidiEventParams_t midiEventParams)
{
    //This function handles the removal of an
    //existing midi event from the virtual grid.

    //IMPORTANT:
    //When it comes to note event types, the function 
    //ONLY handles note-on events. If a note-off
    //event node is received it will be considered
    //as a system fault.
    //When removing a note-on event, the corresponding
    //note-off event will be removed automatically.

    assert(CLEAR_LOWER_NIBBLE(midiEventParams.statusByte) != MIDI_NOTE_OFF_MSG);
    assert(g_GridData.gridLinkedListHeadPtrs[midiEventParams.gridRow] != NULL);
    assert(g_GridData.gridLinkedListTailPtrs[midiEventParams.gridRow] != NULL);
    //TODO: ADD FURTHER CHECKS HERE LATER

    GridEventNode_t * nodeForRemovalPtr = getPointerToEventNodeIfExists(midiEventParams.statusByte, midiEventParams.gridRow, midiEventParams.gridColumn);
    assert(nodeForRemovalPtr != NULL); //We shouldnt ever be trying to remove nodes that dont exist- FAULT CONDITION

    GridEventNode_t * nodePtr = NULL;

    switch(CLEAR_LOWER_NIBBLE(nodeForRemovalPtr->statusByte))
    {
        case MIDI_NOTE_ON_MSG:
            nodePtr = getPointerToCorespondingNoteOffEventNode(nodeForRemovalPtr);
            assert(nodePtr != NULL); //A missing note-off is a system fault
            //Handle note-on event node removal
            genericDLL_deleteNodeFromList(nodeForRemovalPtr, &g_GridData.gridLinkedListHeadPtrs[midiEventParams.gridRow], 
                                          &g_GridData.gridLinkedListTailPtrs[midiEventParams.gridRow]);
            //Handle corresponding note-off event node removal
            genericDLL_deleteNodeFromList(nodePtr, &g_GridData.gridLinkedListHeadPtrs[midiEventParams.gridRow], 
                                          &g_GridData.gridLinkedListTailPtrs[midiEventParams.gridRow]);
            break;

        default:
            assert(0);
            break;
    }
}


//---- Public Interface
MidiEventParams_t getNoteParamsIfCoordinateFallsWithinExistingNoteDuration(uint16_t columnNum, uint8_t rowNum, uint8_t midiChannel)
{
    //Notes on the grid can have multiple step durations, so we need
    //to make sure NOT to place a new note-on note-off pair such that 
    //it overlaps an existing note duration on the same row, to do so
    //would introduce condition considered to be a system fault.

    //RETURNS: IF the target grid coordinate is found to exist witin
    //an existing notes duration a the a populates midiEvent_t struct
    //is returned. ELSE the retured struct is zeroed APART FROM the 
    //stepsToNext member (which may or may not be zero).

    GridEventNode_t * nodePtr = NULL;
    GridEventNode_t * noteOnPtr = NULL;
    MidiEventParams_t eventParams = {0};
    bool skipSearch = false;

    if(g_GridData.gridLinkedListHeadPtrs[rowNum] == NULL) skipSearch = true;

    if(!skipSearch)
    {
        nodePtr = g_GridData.gridLinkedListHeadPtrs[rowNum];

        while(1)
        {
            //TODO: ADD TIMEOUT

            if(CLEAR_LOWER_NIBBLE(nodePtr->statusByte) == MIDI_NOTE_ON_MSG)
            {
                if(CLEAR_UPPER_NIBBLE(nodePtr->statusByte) == midiChannel)
                {
                    if(nodePtr->column <= columnNum)
                    {
                        //If we detect two consectutive
                        //note-on events without a note-off
                        //event between - fault condition.
                        if(noteOnPtr != NULL) assert(0);
                        noteOnPtr = nodePtr;
                    }
                }
            }
            else if(CLEAR_LOWER_NIBBLE(nodePtr->statusByte) == MIDI_NOTE_OFF_MSG)
            {
                if(CLEAR_UPPER_NIBBLE(nodePtr->statusByte) == midiChannel)
                {
                    if(nodePtr->column <= columnNum)
                    {
                        //If we detect two consectutive
                        //note-off events without a note-on
                        //event between - fault condition.
                        if(noteOnPtr == NULL) assert(0);
                        noteOnPtr = NULL;
                    }
                }
            }

            if(nodePtr->nextPtr == NULL) break;
            nodePtr = nodePtr->nextPtr;
            if(nodePtr->column > columnNum) break;
        } 

        if(noteOnPtr != NULL)
        {
            //We must have found a note-on event node to reach here
            assert(CLEAR_LOWER_NIBBLE(noteOnPtr->statusByte) == MIDI_NOTE_ON_MSG);
            assert(CLEAR_UPPER_NIBBLE(noteOnPtr->statusByte) == midiChannel);

            eventParams.statusByte = noteOnPtr->statusByte;
            eventParams.gridColumn = noteOnPtr->column;
            eventParams.gridRow = rowNum;
            memcpy(&eventParams.dataBytes, &noteOnPtr->dataBytes, MAX_DATA_BYTES);
            
            //This note-on must have a corresponding note-off we need to get
            //a pointer to that note-on event we can work out duration in steps.
            nodePtr = getPointerToCorespondingNoteOffEventNode(noteOnPtr);
            assert(nodePtr != NULL); //A missing note-off is a system fault
            eventParams.durationInSteps = nodePtr->column - eventParams.gridColumn;

            //Now we need to check if theres another note-on event after the
            //corresponding note-off to determine the max allowed duration in
            //steps allowed for the detected note event.
            nodePtr = getPointerToNextNoteOnEventInListIfOneExists(noteOnPtr);
            if(nodePtr == NULL) eventParams.stepsToNext = 0;
            else eventParams.stepsToNext = nodePtr->column - noteOnPtr->column;
        }
        else
        {
            eventParams.stepsToNext = getNumStepsToNextNoteOnAfterCoordinate(columnNum, rowNum, midiChannel);
        }

    }

    return eventParams;
}


//---- Public Interface
void updateMidiEventParameters(MidiEventParams_t eventParams)
{

    GridEventNode_t * nodeToUpdatePtr = getPointerToEventNodeIfExists(eventParams.statusByte, eventParams.gridRow, eventParams.gridColumn);

    assert(nodeToUpdatePtr != NULL); //Shouldnt be trying to update events that dont exist

    switch(CLEAR_LOWER_NIBBLE(eventParams.statusByte))
    {
        case MIDI_NOTE_ON_MSG:
            //Once a note-on has been placed only its velocity and duration
            //can be edited. If the note-on needs its row or column changed
            //it should be deleted and a new note placed at the desired coordinate.
            //MORE EDITING FEATURES WILL BE ADDED LATER!
            nodeToUpdatePtr->dataBytes[MIDI_VELOCITY_IDX] = eventParams.dataBytes[MIDI_VELOCITY_IDX];

            //We may also need to update the grid column of the 
            //corresponding note-off message if the note duration
            //has been changed. 
            GridEventNode_t * correspondingNoteOffNodePtr = getPointerToCorespondingNoteOffEventNode(nodeToUpdatePtr);
            assert(correspondingNoteOffNodePtr != NULL);  //A missing note-off is a system fault
            correspondingNoteOffNodePtr->column = nodeToUpdatePtr->column + eventParams.durationInSteps;
            break;

        default:
            assert(0);
            break;
    }

}


//---- Public Interface
void printAllLinkedListEventNodesFromBase(uint16_t rowNum)
{
    //This helper function provides a quick method
    //to print a list structure to console. Its used
    //only for debugging purposes.

    GridEventNode_t * tempNodePtr = NULL;
    uint16_t nodeCount = 0;

    //If list for this row has no event nodes abort operation
    if(g_GridData.gridLinkedListHeadPtrs[rowNum] == NULL) return;

    //Grab direct pointer to the head node of the list
    tempNodePtr = g_GridData.gridLinkedListHeadPtrs[rowNum];

    while(1)
    {
        ++nodeCount;
        ESP_LOGI(LOG_TAG, "\n");
        ESP_LOGI(LOG_TAG, "Event node position in list: %d", nodeCount);
        ESP_LOGI(LOG_TAG, "Event status: %0x", tempNodePtr->statusByte);
        ESP_LOGI(LOG_TAG, "DeltaTime: %ld", tempNodePtr->deltaTime);
        ESP_LOGI(LOG_TAG, "Column: %d", tempNodePtr->column);
        ESP_LOGI(LOG_TAG, "\n");
        if(tempNodePtr->nextPtr == NULL) break;
        else tempNodePtr = tempNodePtr->nextPtr;
        //TODO: ADD TIMEOUT
    }
}


//---- Public Interface
uint32_t gridDataToMidiFile(uint8_t * midiFileBufferPtr, uint32_t bufferSize)
{
    //This function converts the current grid 
    //data to a valid midi file which can then
    //be saved. It DOES NOT handle any file system
    //operations, thats up to the caller to manage.

    //It expects a pointer to the BASE of a previously allocated 
    //midi file buffer to which the new midi file should be written.
    //Any previously existing data within the file buffer will
    //be erased before the new file is generated.

    //RETURNS: The total size of the newly generated midi file in bytes.

    //TODO: Add checks to make sure file buffer size not exceeded!

    uint32_t deltaTime;
    uint32_t trackChunkSizeInBytes;
    GridEventNode_t * tempGridtempNodePtr = NULL;
    bool stillMoreNodesToProccessAtCurrentCoordinate = false;

    assert(midiFileBufferPtr != NULL);
    assert(g_GridData.totalGridColumns > 0);

    memset(midiFileBufferPtr, 0, bufferSize);
    generateEmptyMidiFile(midiFileBufferPtr, g_GridData.sequencerPPQN, 120);
    generateDeltaTimesForCurrentGrid();

    const uint8_t * trackChunkBasePtr = (midiFileBufferPtr + MIDI_FILE_TRACK_HEADER_OFFSET);
    midiFileBufferPtr += MIDI_FILE_MIDI_EVENTS_OFFSET;

    //We're going to process the entire grid one grid coordinate at a time, the amount of grid rows is fixed,
    //but the amount of columns used varies depending on the length of the sequence. For each column in the
    //grid we will scan through all rows looking for nodes that match the current target coordinate.
    for(uint16_t currentTargetColumn = 0; currentTargetColumn <= g_GridData.totalGridColumns; ++currentTargetColumn)
    {
        //Iterate through all grid rows. Each row stores midi events as nodes 
        //in a linked list. A row may have zero or more event nodes.
        for(uint8_t currentRow = 0; currentRow < TOTAL_MIDI_NOTES; ++currentRow)
        {   

            //We only need to do processing if the current grid row
            //has nodes/events allocated - skip if no row has no events
            if(g_GridData.gridLinkedListHeadPtrs[currentRow] != NULL)
            {

                //Grab a direct pointer to the linked list of the current row
                tempGridtempNodePtr = g_GridData.gridLinkedListHeadPtrs[currentRow];

                //Search the current row for any events which
                //have grid coorinates which match the current target
                while(tempGridtempNodePtr->column < currentTargetColumn)
                {
                    if(tempGridtempNodePtr->nextPtr == NULL) break;
                    tempGridtempNodePtr = tempGridtempNodePtr->nextPtr;
                }

                //We only need to do processing if we've found
                //an event node at the current target coordinate
                if(tempGridtempNodePtr->column == currentTargetColumn)
                {

                    do
                    {
                        //We only reach here when an event node exists at a grid
                        //coordinate which matches the current target coordinate

                        //Only one type of event midi event can exist at 
                        //any one grid coordinate, but it is possible to
                        //have multiple events of different types which
                        //do exist at the same coordinate.
                        stillMoreNodesToProccessAtCurrentCoordinate = false;

                        //ESP_LOGI(LOG_TAG, "CurrentTargetColumn: %d", currentTargetColumn);
                        //ESP_LOGI(LOG_TAG, "Node column: %d", tempGridtempNodePtr->column);
                        //ESP_LOGI(LOG_TAG, "CurrentRow: %0x", currentRow);

                        //Grab the delta-time of the current midi event node
                        deltaTime = tempGridtempNodePtr->deltaTime;

                        //We now need to convert the delta-time of the
                        //current node/event being processed to a midi file
                        //format delta-time (a variable length value)
                        if(deltaTime > MAX_DELTA_TIME_BYTE_VALUE)
                        {
                            //The 'deltaTimeBuffer' will be used as a lifo byte buffer,
                            //that will act as temp storage for variable length encoded
                            //midi file format delta-time bytes which are generated below. 

                            register uint32_t deltaTimeBuffer = CLEAR_MSBIT_IN_BYTE(deltaTime);
                            while(deltaTime >>= (NUM_BITS_IN_BYTE - 1))
                            {
                                deltaTimeBuffer <<= NUM_BITS_IN_BYTE;
                                deltaTimeBuffer |= (CLEAR_MSBIT_IN_BYTE(deltaTime) | (1 << (NUM_BITS_IN_BYTE - 1)));
                            }

                            //The LSB of 'deltaTimeBuffer' is the MSB of the
                            //variable length encoded delta-time. The encoded
                            //delta-time bytes are now written to file MSB first
                            uint8_t rawDeltaTimeByteCount = 0;
                            while(rawDeltaTimeByteCount < MIDI_FILE_MAX_DELTA_TIME_NUM_BYTES)
                            {
                                *midiFileBufferPtr = (uint8_t)deltaTimeBuffer; //Write encoded delta-time byte to file
                                ++rawDeltaTimeByteCount;
                                ++midiFileBufferPtr;
                                if (GET_MSBIT_IN_BYTE(deltaTimeBuffer)) deltaTimeBuffer >>= NUM_BITS_IN_BYTE;
                                else break;
                            }
                        }
                        else
                        {
                            //The current delta-time value is small
                            //enough that it requires no encoding
                            //so it can be written directly to file
                            *midiFileBufferPtr = (uint8_t)deltaTime;
                            ++midiFileBufferPtr;
                        }

                        //At the moment only note events and EOF meta event are supported,
                        //this code will be modified later to support other event types
                        *midiFileBufferPtr = tempGridtempNodePtr->statusByte;
                        ++midiFileBufferPtr;
                        *midiFileBufferPtr = tempGridtempNodePtr->dataBytes[MIDI_NOTE_NUM_IDX];
                        ++midiFileBufferPtr;
                        *midiFileBufferPtr = tempGridtempNodePtr->dataBytes[MIDI_VELOCITY_IDX];
                        ++midiFileBufferPtr;

                        //Check whether this node is the last
                        //in the linked list for the current row
                        if(tempGridtempNodePtr->nextPtr != NULL)
                        {
                            //If there are more nodes in the list we need to check for 
                            //the case when multiple nodes exist at the same cooridnate
                            if(tempGridtempNodePtr->nextPtr->column == tempGridtempNodePtr->column)
                            {
                                //There are multiple events/nodes which 
                                //share the same grid co-ordinates so update
                                //the node pointer onto next node and set flag
                                tempGridtempNodePtr = tempGridtempNodePtr->nextPtr;
                                stillMoreNodesToProccessAtCurrentCoordinate = true;
                            }
                        }
                    //Loop until all nodes with coordinates that match
                    //the current target coordinate have been processed
                    }while(stillMoreNodesToProccessAtCurrentCoordinate);
                }
            }
        }
    }

    //Manually add the EOF meta event
    *midiFileBufferPtr = MIDI_EOF_EVENT_BYTE0;
    ++midiFileBufferPtr;
    *midiFileBufferPtr = MIDI_EOF_EVENT_BYTE1;
    ++midiFileBufferPtr;
    *midiFileBufferPtr = MIDI_EOF_EVENT_BYTE2;
    ++midiFileBufferPtr;
    *midiFileBufferPtr = MIDI_EOF_EVENT_BYTE3;
    ++midiFileBufferPtr;

    //Determine the total size of the midi track data NOT including the track header.
    //The calculated value will then need to be written to the 'size' field of the track header.
    trackChunkSizeInBytes = midiFileBufferPtr - trackChunkBasePtr;
    trackChunkSizeInBytes -= (MIDI_TRACK_HEADER_NUM_BYTES + MIDI_FILE_TRACK_SIZE_FIELD_NUM_BYTES);

    //Set the file buffer pointer the the base of
    //the four byte 'size' field within the track header
    midiFileBufferPtr = (trackChunkBasePtr + MIDI_TRACK_HEADER_NUM_BYTES);

    //Now write the four byte size field of the track header to file buffer
    for(int8_t a = (MIDI_FILE_TRACK_SIZE_FIELD_NUM_BYTES-1); a >= 0 ; --a)
    {
        *midiFileBufferPtr = (uint8_t)(trackChunkSizeInBytes >> (a * NUM_BITS_IN_BYTE));
        ++midiFileBufferPtr;
    }

    //Return the total size in bytes of the generated midi file
    return (trackChunkSizeInBytes + MIDI_FILE_MIDI_EVENTS_OFFSET);
}


//---- Public Interface
void midiFileToGrid(uint8_t * midiFileBufferPtr, uint32_t bufferSize)
{
    //This function converts an existing midi 
    //file to a grid compatable data structure.

    //It expects a pointer to the BASE of
    //a previously existing midi file.

    //TODO: Add checks to make sure file buffer size not exceeded!

    uint32_t currentDeltaTime = 0;
    uint16_t totalColumnCount = 0;
    uint8_t deltaTimeNumBytes;
    uint8_t statusByte;
    uint8_t midiVoiceMsgData[2];
    int8_t metaMsgLength;
    bool corruptFileDetected = false;
    MidiEventParams_t newEventParams = {0};

    assert(midiFileBufferPtr != NULL);
    assert(getMidiFileFormatType(midiFileBufferPtr) == MIDI_FILE_FORMAT_TYPE0);

    freeAllGridData();
    g_GridData.totalGridColumns = 0;

    //Set the pointer to the BASE of the 
    //first midi event before processing
    midiFileBufferPtr += MIDI_FILE_MIDI_EVENTS_OFFSET;

    while(1)
    {
        currentDeltaTime = processMidiFileDeltaTime(midiFileBufferPtr);
        deltaTimeNumBytes = getDeltaTimeVariableLengthNumBytes(currentDeltaTime);
        //Handle incrementing file ptr after reading current events delta-time
        if(deltaTimeNumBytes <= MIDI_FILE_MAX_DELTA_TIME_NUM_BYTES) midiFileBufferPtr += deltaTimeNumBytes;
        else
        {
            corruptFileDetected = true;
            break;
        }

        totalColumnCount += currentDeltaTime / ((g_GridData.sequencerPPQN * NUM_QUATERS_IN_WHOLE_NOTE) / g_GridData.projectQuantization);

        statusByte = *midiFileBufferPtr;

        if(statusByte == MIDI_META_MSG) //--- Meta Message Type ---//
        {
            metaMsgLength = processMidiFileMetaMessage(midiFileBufferPtr);

            if(metaMsgLength == 0)
            {
                break; //End of file detected
            }
            else if(metaMsgLength < 0)
            {
                corruptFileDetected = true;
                break;
            }
            else
            {
                //Ignore meta messages for now but we still need to increment
                //the file pointer onto the next event in the midi file.
                midiFileBufferPtr += metaMsgLength + MIDI_META_MESSAGE_SIZE;
            }
        }
        else if((statusByte >= VOICE_MSG_STATUS_RANGE_MIN) && (statusByte <= VOICE_MSG_STATUS_RANGE_MAX)) //--- Voice Message Type ---//
        {
            
            for(uint8_t a = 0; a < MAX_MIDI_VOICE_MSG_DATA_BYTES; ++a)
            {
                ++midiFileBufferPtr;
                midiVoiceMsgData[a] = *midiFileBufferPtr;
            }
            ++midiFileBufferPtr;


            switch (CLEAR_LOWER_NIBBLE(statusByte))
            {

                case 0x80: //---Note Off---//
                case 0x90: //---Note On----//
                    //Format (n = channel number)
                    //byte[0] = 0x9n OR 0x8n
                    //Byte[1] = Note Number
                    //Byte[2] = Velocity

                    //file pointer now points to base of next event
                    //no need to reference it again for current event

                    //The range of possible midi notes is 0->127
                    //so ignore anything outside that range
                    if(midiVoiceMsgData[MIDI_NOTE_NUM_IDX] < TOTAL_MIDI_NOTES)
                    {
                        //Construct a double linked list for each row of sequencer used
                        //each linked list node is a struct containing pointers and midi data

                        //IMPORTANT: A note duration of zero indicates to the function that
                        //corresponding note-off events shouldnt be automatically added, which
                        //is required while performing a midiFileToGrid conversion.
                        newEventParams.statusByte = statusByte;
                        newEventParams.dataBytes[MIDI_NOTE_NUM_IDX] = midiVoiceMsgData[MIDI_NOTE_NUM_IDX];
                        newEventParams.dataBytes[MIDI_VELOCITY_IDX] = midiVoiceMsgData[MIDI_VELOCITY_IDX];
                        newEventParams.gridRow = midiVoiceMsgData[MIDI_NOTE_NUM_IDX];
                        newEventParams.gridColumn = totalColumnCount;
                        newEventParams.durationInSteps = 0;

                        addNewMidiEventToGrid(newEventParams);
                        
                        //ESP_LOGI(LOG_TAG, "\n");
                        //ESP_LOGI(LOG_TAG, "New %s midi event detected..", (CLEAR_LOWER_NIBBLE(statusByte) == 0x90) ? "Note-On" : "Note-Off");
                        //ESP_LOGI(LOG_TAG, "Event delta-time: %ld", currentDeltaTime);
                        //ESP_LOGI(LOG_TAG, "NoteNumber (decimal): %d, (hex): %0x", midiVoiceMsgData[0], midiVoiceMsgData[0]);
                        //ESP_LOGI(LOG_TAG, "Velocity (decimal): %d, (hex): %0x", midiVoiceMsgData[1], midiVoiceMsgData[1]);
                        //ESP_LOGI(LOG_TAG, "TotalColumnCount: %d", totalColumnCount);
                    }
                    else
                    {
                        ESP_LOGE(LOG_TAG, "Error: Out of range midi note detected");
                        corruptFileDetected = true;
                    }
                    break;


                case 0xA0: //---Aftertouch---//
                    //Format (n = channel number)
                    //Byte[0] = 0xAn
                    //Byte[1] = Note Number
                    //Byte[2] = Pressure Value
                    midiFileBufferPtr += 3;
                    break;

                case 0xB0: //---Control Change---//
                    //Format (n = channel number)
                    //Byte[0] = 0xBn
                    //Byte[1] = Control opcode
                    //Byte[2] = Value
                    midiFileBufferPtr += 3;
                    break;

                case 0xC0: //---Program Change---//
                    //Format (n = channel num)
                    //Byte[0] = 0xCn
                    //Byte[1] = Program value
                    midiFileBufferPtr += 2;
                    break;

                case 0xD0: //---Channel Pressure---//
                    //Format (n = channel num)
                    //Byte[0] = 0xDn
                    //Byte[1] = Pressure value
                    midiFileBufferPtr += 2;
                    break;

                case 0xE0: //---Pitch Wheel---//
                    //Format (n = channel num)
                    //Byte[0] = 0xEn
                    //Byte[1] = Pitch Value MSB (these two bytes must each have bit 8 removed, concatenate result)
                    //Byte[2] = Pitch Value LSB
                    midiFileBufferPtr += 3;
                    break;

                default: //--- ERROR ---//
                    ESP_LOGE(LOG_TAG, "Error: Unrecognised midi voice msg in 'midiFileToGrid'");
                    corruptFileDetected = true;
                    break;
            }

            if(corruptFileDetected) break;
        }
        else
        {
            //In order to reach here the playback pointer has
            //reached a status byte that it doesn't recognise.
            //This could be a 'running status', where subsequent
            //events of the same type may ommit the status byte
            ESP_LOGE(LOG_TAG, "Error: Running status detected - not supported");
            corruptFileDetected = true;
            break;
        }
    }

    if(corruptFileDetected) assert(0);
    else
    {
        ESP_LOGI(LOG_TAG, "midiFileToGrid SUCCESS, total columns in project: %d", totalColumnCount);
        g_GridData.totalGridColumns = ++totalColumnCount; //Add one to remove zero base
    }
}


//---- Public Interface 
void updateGridLEDs(uint8_t rowOffset, uint16_t columnOffset)
{
    //This function updates all rgbs leds of the switch matrix 

    GridEventNode_t * tempNodePtr = NULL;
    GridEventNode_t * searchPtr = NULL;
    bool abortCurrentRow = false;
    bool runOncePerRow = false;
    uint16_t numGridSteps = 0;
    uint8_t relativeRow = 0;
    uint16_t relativeColumn = 0;
    rgbLedColour_t gridRGBCodes[48] = {rgb_off};

    bool keepSearchingCurrentRow = false;
    bool withinNoteDuration = true;

    //The pysical sequencer grid is made up of a matrix of switches, where each
    //switch has its own assosiated RGB led, this function handles the setting
    //of those RGB leds in order to provide a means to display grid data.

    //The pysical size of the grids switch matrix is defined by:
    //NUM_SEQUENCER_PHYSICAL_ROWS x NUM_SEQUENCER_PHYSICAL_COLUMNS
    //The top left switch of the grid considered as coordinate 0,0.

    //The grid data that can be displayed is limited to the physical size of
    //the sequencer grid. Depending on the sequence, its likely that it will
    //only be possible to display a subset of the virtual grid at any one time.

    //The input arguments 'rowOffset' and 'columnOffset' allow the caller to
    //specify which area of grid data should be displayed on the physical grid.

    //EXAMPLE: rowOffset = 5, columnOffset = 7.
    //The physical grid will display any events which fall 
    //within the following area of the virtual grid data.
    //Rows:    5 -> (5 + (NUM_SEQUENCER_PHYSICAL_ROWS - 1))
    //Columns: 7 -> (7 + (NUM_SEQUENCER_PHYSICAL_COLUMNS - 1))

    //NOTE: Current version is hardcoded to display channel 0 note only ATM
    //will add input arguments for channel and other midi event types after v0.1

    //Range check the number of rows, maybe add limit for columns later
    assert(rowOffset <= ((TOTAL_NUM_VIRTUAL_GRID_ROWS - 1) - (NUM_SEQUENCER_PHYSICAL_ROWS - 1)));

    //Iterate through each grid row that fall within the specified area. Each row 
    //stores midi events as nodes in a linked list. A row may have zero or more event nodes.
    for(uint8_t rowNum = rowOffset; rowNum < (rowOffset + NUM_SEQUENCER_PHYSICAL_ROWS); ++rowNum)
    { 
        runOncePerRow = true;
        abortCurrentRow = false;

        //We only need to do further processing for the 
        //current row if has midi events allocated to it 
        if(g_GridData.gridLinkedListHeadPtrs[rowNum] != NULL)
        {
            //A NULL pointer here indcates a system fault
            assert(g_GridData.gridLinkedListTailPtrs[rowNum] != NULL);

            //If the column of the last event node in the current rows list is LESS 
            //than the columnOffset no further processing is required for the current row
            if(g_GridData.gridLinkedListTailPtrs[rowNum]->column >= columnOffset)
            {
                //Grab a direct ptr to current rows linked list
                tempNodePtr = g_GridData.gridLinkedListHeadPtrs[rowNum];

                do
                {
                    keepSearchingCurrentRow = false;

                    //These conditionals are used to catch a condition where a note-on and note-off
                    //pair exist before and after (respectively) the area of the virtual grid to be displayed,
                    //without these a condition where an entire row should be illuminated, but isnt can occur.
                    if(CLEAR_LOWER_NIBBLE(tempNodePtr->statusByte) == MIDI_NOTE_ON_MSG) withinNoteDuration = true;
                    else if(CLEAR_LOWER_NIBBLE(tempNodePtr->statusByte) == MIDI_NOTE_OFF_MSG) withinNoteDuration = false;

                    if((tempNodePtr->column >= columnOffset) && (tempNodePtr->column < (columnOffset + NUM_SEQUENCER_PHYSICAL_COLUMNS)))
                    {
                        //We reach here if the current node falls
                        //within the grid area that we want to display

                        //remove offset to get zero base hardware column num
                        relativeColumn = tempNodePtr->column - columnOffset;
            
                        if((CLEAR_LOWER_NIBBLE(tempNodePtr->statusByte) == MIDI_NOTE_OFF_MSG) && (runOncePerRow))
                        {
                            //We only get here when the first event node within the area to be displayed is a note off event
                            //while note-off events are not themselves displayed, if the relativeColumn is greater than zero
                            //it means we have an existing note duration that overruns into the target window.
                            //Any grid steps that are a note duration overrun must be displayed.

                            numGridSteps = tempNodePtr->column - columnOffset;
                            runOncePerRow = false;

                            for(uint8_t a = 0; a < numGridSteps; ++a)
                            {
                                //Set the colour of the grid coordinates that make up the note duration overrun
                                gridRGBCodes[0 + (relativeRow * NUM_SEQUENCER_PHYSICAL_COLUMNS) + a] = rgb_green;
                                //if((relativeColumn + a) > NUM_SEQUENCER_PHYSICAL_COLUMNS) break;
                            }
                        }
                        else if(CLEAR_LOWER_NIBBLE(tempNodePtr->statusByte) == MIDI_NOTE_ON_MSG)
                        {
                            //A note-on event has been found within the target window
                            //we must now find the duration of that note off event in steps

                            //Search for and get pointer to corresponding note-off
                            searchPtr = getPointerToCorespondingNoteOffEventNode(tempNodePtr);
                            if(searchPtr != NULL) 
                            {
                                //We get here if the corresponding note-off to 
                                //the current note-on event has been found to exist
                                numGridSteps = searchPtr->column - tempNodePtr->column;

                            }
                            else
                            {
                                assert(0);  //A missing note-off is a system fault
                            } 

                            for(uint8_t a = 0; a < numGridSteps; ++a)
                            {
                                //Set the colour of the grid coordinates that make up the current notes duration
                                if((relativeColumn + a) > (NUM_SEQUENCER_PHYSICAL_COLUMNS - 1)) break;
                                gridRGBCodes[relativeColumn + (relativeRow * NUM_SEQUENCER_PHYSICAL_COLUMNS) + a] = rgb_green;
                            }

                            tempNodePtr = searchPtr;
                        }
                    }

                    if(tempNodePtr->nextPtr !=  NULL) //If there are more nodes in the current rows list
                    {
                        //If the column number of the next node doesnt exceed the maximum
                        //area we can display then we need to update tempNodePtr and keep searching
                        if(tempNodePtr->nextPtr->column < (columnOffset + NUM_SEQUENCER_PHYSICAL_COLUMNS))
                        {
                            tempNodePtr = tempNodePtr->nextPtr;
                            keepSearchingCurrentRow = true;
                        }
                        else if(withinNoteDuration)
                        {
                            for(uint8_t a = 0; a < NUM_SEQUENCER_PHYSICAL_COLUMNS; ++a)
                            {
                                //Light all steps in the row
                                gridRGBCodes[0 + (relativeRow * NUM_SEQUENCER_PHYSICAL_COLUMNS) + a] = rgb_green;
                            }
                        }
                    }

                }while(keepSearchingCurrentRow);
            }
        }
        ++relativeRow; //Increment zero offset hardware row
    }

    ledDrivers_writeEntireGrid(gridRGBCodes);
}





//----------------------------------------------
//-------- PRIVATES AFTER THIS POINT -----------
//----------------------------------------------


//---- Private
static void freeAllGridData(void)
{
    //This function frees the entire virtual
    //grid data structure, which is made up
    //of an array of double linked lists.

    //After this function executes the grid
    //data structure is in its reset state 
    //and ready for a new project to start.

    for(uint8_t a = 0; a < TOTAL_MIDI_NOTES; ++a)
    {   
        genericDLL_freeEntireLinkedList(&g_GridData.gridLinkedListHeadPtrs[a], &g_GridData.gridLinkedListTailPtrs[a]);
    }
}


//---- Private
static void generateDeltaTimesForCurrentGrid(void)
{
    //This function will assign midi delta-times to all event nodes
    //currenly on the grid - its much easier to process the whole grid
    //before playback or file save than to construct and edit delta-times
    //on the fly as grid events are added.

    uint16_t previousColumn = 0;
    GridEventNode_t * tempNodePtr = NULL;

    for(uint16_t currentTargetColumn = 0; currentTargetColumn <=g_GridData.totalGridColumns; ++currentTargetColumn)
    {
        
        for(uint8_t currentMidiNote = 0; currentMidiNote < TOTAL_MIDI_NOTES; ++currentMidiNote) 
        {   
            //We only need to do furth processing if the current rows 
            //linked list has one of more midi event nodes allocated to it
            if(g_GridData.gridLinkedListHeadPtrs[currentMidiNote] != NULL)
            {

                //Grad a direct pointer to the current rows linked list
                tempNodePtr = g_GridData.gridLinkedListHeadPtrs[currentMidiNote];

                //As long as this event node doesnt exist at 
                //a column beyong the current target then process
                if(tempNodePtr->column <= currentTargetColumn)
                {
                    //Search linked list for midi event node at column that matches current target column
                    while(tempNodePtr->nextPtr != NULL && tempNodePtr->column < currentTargetColumn)
                    {
                        tempNodePtr = tempNodePtr->nextPtr;
                    }
                    //If the column of this midi event node matches
                    //the current target then we need to process
                    if(tempNodePtr->column == currentTargetColumn)
                    {
                        //When generating delta-times columns are processed one at 
                        //a time where the delta-times for all rows in the target column
                        //are generated before the next column is processed.
                        //Columns are processed from 0 -> g_GridData.totalGridColumns
                        //each column represents a duration of step time, hence why
                        //columns are processed sequentially while generating the 
                        //timing data for each midi event.

                        //A midi event node at the target column in the virtual grid
                        //has been found we now need to generate its delta-time value.
                        tempNodePtr->deltaTime = (tempNodePtr->column - previousColumn) * ((g_GridData.sequencerPPQN * NUM_QUATERS_IN_WHOLE_NOTE) / g_GridData.projectQuantization);
                        previousColumn = currentTargetColumn;

                        //We just found a midi event node at the target column
                        //in the virtual grid, but it is possible for multiple
                        //event nodes to be placed at the same grid coordinate
                        //the only condition being that no duplicates of a single
                        //midi event type. (Only one Note-on, only one note-off, etc).
                        //NOTE: Any of there midi event nodes at the current target
                        //virtual grid coordinate will be set delta-time 0 as they
                        //should all occur at the same time.
                        while(tempNodePtr->nextPtr != NULL)
                        {
                            tempNodePtr = tempNodePtr->nextPtr;
                            //We break from the loop as soon as the 
                            //column pointed at tempNodePtr exists at
                            //a column beyond the current target column.
                            if(tempNodePtr->column == currentTargetColumn)
                            {
                                tempNodePtr->deltaTime = 0;
                            }
                            else break;
                        }
                    }
                }
            }
        }
    }
}


//---- Private
static void addCorrespondingNoteOff(GridEventNode_t * noteOnNode, uint16_t noteDuration)
{
    //The functions handles the automatic generation of note-off
    //midi events for a corresponding note-on event at the 
    //appropriate virtual grid coordinate. Its a system requirement
    //that a corresponding note-off event is created each time a new
    //midi note-on event is added to the grid by the user.

    //IMPORTANT: Its the callers responsibility to make sure that
    //its safe to add a midi note-off event at the resultant
    //coordinate accoriding to system requirements.

    assert(noteOnNode != NULL);
    assert(CLEAR_LOWER_NIBBLE(noteOnNode->statusByte) == MIDI_NOTE_ON_MSG);

    GridEventNode_t * noteOffNodePtr = NULL;

    uint8_t midiEventChannel = CLEAR_UPPER_NIBBLE(noteOnNode->statusByte);
    uint8_t noteOffStatusByte = MIDI_NOTE_OFF_MSG | midiEventChannel;
    uint16_t noteOffColumn = noteOnNode->column + noteDuration;

    noteOffNodePtr = genericDLL_createNewNode();
    assert(noteOffNodePtr != NULL);
    //Assign event parameters
    noteOffNodePtr->statusByte = noteOffStatusByte;
    noteOffNodePtr->column = noteOffColumn;
    noteOffNodePtr->dataBytes[MIDI_NOTE_NUM_IDX] = noteOnNode->dataBytes[MIDI_NOTE_NUM_IDX];
    noteOffNodePtr->dataBytes[MIDI_VELOCITY_IDX] = MIDI_MAX_VELOCITY;
    //TODO: ADD RGB COLOUR CODE ASSIGNMENT - CURRENTLY HARDCODED

    if(genericDLL_returnTrueIfLastNodeInList(noteOnNode))
    {
        //The note-off event node we want to add will be appended onto a list
        genericDLL_appendNewNodeOntoLinkedList(noteOffNodePtr, &g_GridData.gridLinkedListHeadPtrs[noteOnNode->dataBytes[MIDI_NOTE_NUM_IDX]], 
                                               &g_GridData.gridLinkedListTailPtrs[noteOnNode->dataBytes[MIDI_NOTE_NUM_IDX]]);

        //Update a record of the total columns in the project
        if(noteOffNodePtr->column > g_GridData.totalGridColumns) g_GridData.totalGridColumns = noteOffNodePtr->column;

    }
    else
    {
        //The note-off event node we want to add will be inserted into a list
        genericDLL_insertNewNodeIntoLinkedList(noteOffNodePtr, noteOnNode, &g_GridData.gridLinkedListHeadPtrs[noteOnNode->dataBytes[MIDI_NOTE_NUM_IDX]]);
    }
}


//---- Private
static GridEventNode_t * getPointerToCorespondingNoteOffEventNode(GridEventNode_t * noteOnEventPtr)
{
    //This function seaches a rows linked list in forward direction 
    //from a supplied note-on event node for a corresponding note-off
    //event which has the same midi channel number as the input node.

    //Note-on and Note-off events are always created in pairs
    //if one exists without the other its a fault condition!

    //REQUIREMENTS: This function expects a note-on event node ptr,
    //if any other type is passed it will be considered a fault
    //condition and generate an assertion failure.

    //RETURNS: A pointer to the corresponding note-off event node.
    //If no correponding note-off is found OR another note-on event
    //is encountered (with matching midi channel number) first then
    //will return NULL.

    assert(noteOnEventPtr != NULL);
    assert(CLEAR_LOWER_NIBBLE(noteOnEventPtr->statusByte) == MIDI_NOTE_ON_MSG);

    GridEventNode_t * nodePtr = noteOnEventPtr;

    while(nodePtr->nextPtr != NULL)
    {
        //TODO: ADD TIMEOUT
        nodePtr = nodePtr->nextPtr;
        if(CLEAR_LOWER_NIBBLE(nodePtr->statusByte) == MIDI_NOTE_OFF_MSG)
        {
            //We found a note-off event, we now need to see if
            //it has the same channel number as the note-on event
            if(CLEAR_UPPER_NIBBLE(noteOnEventPtr->statusByte) == CLEAR_UPPER_NIBBLE(nodePtr->statusByte))
            {
                return nodePtr; //Found corresponding note-off
            }
        } 
        else if(CLEAR_LOWER_NIBBLE(nodePtr->statusByte) == MIDI_NOTE_ON_MSG)
        {
            if(CLEAR_UPPER_NIBBLE(noteOnEventPtr->statusByte) == CLEAR_UPPER_NIBBLE(nodePtr->statusByte))
            {
                return NULL;
            }
        } 
    }

    return NULL;
}


//---- Private
static GridEventNode_t * getPointerToNextNoteOnEventInListIfOneExists(GridEventNode_t * noteOnEventPtr)
{
    //This function seaches a rows linked list in forward direction 
    //from a supplied note-on event node for the next note-on event 
    //in which has the same midi channel number as the input node,
    //IF SUCH A NODE EXISTS.

    //REQUIREMENTS: This function expects a note-on event node ptr,
    //if any other type is passed it will be considered a fault
    //condition and generate an assertion failure.

    //RETURNS: IF another note-on event node with matching midi
    //channel number if found a pointer to it is returned.
    //ELSE returns NULL.

    assert(noteOnEventPtr != NULL);
    assert(CLEAR_LOWER_NIBBLE(noteOnEventPtr->statusByte) == MIDI_NOTE_ON_MSG);

    GridEventNode_t * nodePtr = noteOnEventPtr;

    while(nodePtr->nextPtr != NULL)
    {
        //TODO: ADD TIMEOUT
        nodePtr = nodePtr->nextPtr;
        if(CLEAR_LOWER_NIBBLE(nodePtr->statusByte) == MIDI_NOTE_ON_MSG)
        {
            //We found a note-off event, we now need to see if
            //it has the same channel number as the note-on event
            if(CLEAR_UPPER_NIBBLE(noteOnEventPtr->statusByte) == CLEAR_UPPER_NIBBLE(nodePtr->statusByte))
            {
                return nodePtr; //Found corresponding note-off
            }
        } 
    }

    return NULL;
}


//---- Private
uint8_t getNumStepsToNextNoteOnAfterCoordinate(uint16_t columnNum, uint8_t rowNum, uint8_t midiChannel)
{
    uint8_t numStepsToNext = 0;
    bool abortSearch = false;

    GridEventNode_t * nodePtr = g_GridData.gridLinkedListHeadPtrs[rowNum];
    if(nodePtr == NULL) abortSearch = true;

    if(!abortSearch)
    {
        while(nodePtr->nextPtr != NULL)
        {
            //TODO: ADD TIMEOUT
            nodePtr = nodePtr->nextPtr;
            if(nodePtr->column > columnNum)
            {
                //We're only looking for note-on events 
                //that occur AFTER the input columnNum
                if(CLEAR_LOWER_NIBBLE(nodePtr->statusByte) == MIDI_NOTE_ON_MSG)
                {
                    //We found a note-off event, we now need to see if
                    //it has the same channel number as the note-on event
                    if(midiChannel == CLEAR_UPPER_NIBBLE(nodePtr->statusByte))
                    {
                        numStepsToNext = nodePtr->column - columnNum;
                    }
                } 
            }
        }
    }

    return numStepsToNext;
}


//---- Private
static GridEventNode_t * getPointerToEventNodeIfExists(uint8_t targetStatusByte, uint8_t rowNum, uint16_t columnNum)
{
    //This function iterates through a linked list in the 
    //forward direction, looking for an event node at the 
    //target coordinate which has a matching statusByte

    //RETURNS: IF a node is found, a pointer to it is
    //returned. ELSE a NULL ptr value is returned.

    GridEventNode_t * targetNode = NULL;

    //The linked list for this row has no event nodes
    if(g_GridData.gridLinkedListHeadPtrs[rowNum] == NULL) return NULL;

    //Grab a direct pointer to the HEAD node in the list
    targetNode = g_GridData.gridLinkedListHeadPtrs[rowNum];

    while(1)
    { 
        //TODO: ADD TIMEOUT
        if((targetNode->column == columnNum) && (targetNode->statusByte == targetStatusByte)) return targetNode;
        else if(targetNode->column > columnNum) break;
        if(targetNode->nextPtr == NULL) break;
        else targetNode = targetNode->nextPtr;
    }

    return NULL;
}