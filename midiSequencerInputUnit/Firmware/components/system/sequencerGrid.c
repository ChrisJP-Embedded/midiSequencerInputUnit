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
#include "sequencerGrid.h"
#include "esp_task_wdt.h"
#include "midiHelper.h"


#define LOG_TAG "sequencerGrid"
#define TEMPO_IN_MICRO 500000
#define HAS_MORE_DELTA_TIME_BYTES(X) ((0x80 & X) && (1 << 8))

SequencerGridData_t g_GridData;

void updateGridLEDs(uint8_t rowOffset, uint16_t columnOffset);
static uint8_t generateDeltaTimesForCurrentGrid(void);
static void deleteEventNode(SequencerGridItem_t ** eventNodePtr);
static bool doesThisGridCoordinateFallWithinAnExistingNoteDuration(uint16_t columnNum, uint8_t midiNoteNum);
static SequencerGridItem_t * getPointerToCorespondingNoteOffEventNode(SequencerGridItem_t * baseNodePtr);
static SequencerGridItem_t * getPointerToEventNodeIfExists(uint8_t targetStatusByte, uint16_t columnNum, uint8_t midiNoteNum);
static SequencerGridItem_t * createNewEventNode(uint8_t statusByte, uint16_t columnNum, uint8_t midiNoteNum, uint8_t midiVelocity, uint16_t rgbCode);
static void managePointersAndInsertNewEventNodeIntoLinkedList(SequencerGridItem_t * newEventNodePtr, SequencerGridItem_t * insertLocationPtr, bool isLocationListHeadPtr);
static void managePointersAndAppendNewEventNodeIntoLinkedList(SequencerGridItem_t * newEventNodePtr, uint8_t listIdx);
static uint8_t appendNewGridData(uint16_t columnNum, uint8_t statusByte, uint8_t midiNoteNumber, uint8_t midiVelocity, uint8_t durationInSteps, bool autoAddNoteOff);
static uint8_t insertNewGridData(uint16_t columnNum, uint8_t statusByte, uint8_t midiNoteNumber, uint8_t midiVelocity, uint8_t durationInSteps, bool autoAddNoteOff);
static void freePreviousGridLinkedListHeadPtrs(void);


void resetSequencerGrid(uint8_t ppqn, uint8_t quantization)
{
    //Should be called at startup and immediately
    //before loading any new projects via 'midiFileToGrid'
    g_GridData.projectQuantization = quantization; //Set quantization setting for this new session
    g_GridData.sequencerPPQN = ppqn; //Set the pulses-per-quater-note for this new session
    //Completely clear the current 
    //grid data cache, automatically
    //frees all nodes/events if any exist
    freePreviousGridLinkedListHeadPtrs();
    //The grid is now cleared and ready
    //for new nodes/events to be added
}

static SequencerGridItem_t * createNewEventNode(uint8_t statusByte, uint16_t columnNum, uint8_t midiNoteNum, uint8_t midiVelocity, uint16_t rgbCode)
{
    //Allocate memory for new node from psram
    SequencerGridItem_t * newNodePtr = heap_caps_malloc(sizeof(SequencerGridItem_t), MALLOC_CAP_SPIRAM);
    assert(newNodePtr != NULL);

    memset(newNodePtr, 0, sizeof(SequencerGridItem_t));

    newNodePtr->column = columnNum;
    newNodePtr->statusByte = statusByte;
    newNodePtr->dataBytes[0] = midiNoteNum;
    newNodePtr->dataBytes[1] = midiVelocity;
    newNodePtr->rgbColourCode = rgbCode;
    newNodePtr->prevPtr = NULL;
    newNodePtr->nextPtr = NULL;

    return newNodePtr;
}

static void deleteEventNode(SequencerGridItem_t ** eventNodePtr)
{
    free(*eventNodePtr);
    *eventNodePtr = NULL;
}

static SequencerGridItem_t * getPointerToEventNodeIfExists(uint8_t targetStatusByte, uint16_t columnNum, uint8_t midiNoteNum)
{
    //This function interates through a linked list
    //looking for an event node at the target coordinate
    //which has a status byte matching targetStatusByte.

    //If a node is found, a pointer to it is returned,
    //otherwise, the return value is NULL.

    SequencerGridItem_t * targetNode = NULL;

    if(g_GridData.gridLinkedListHeadPtrs[midiNoteNum] == NULL) return false;

    targetNode = g_GridData.gridLinkedListHeadPtrs[midiNoteNum];

    while(targetNode != NULL)
    {
        if((targetNode->column == columnNum) && (targetNode->statusByte == targetStatusByte)) return targetNode;
        else if(targetNode->column > columnNum) break;
        if(targetNode->nextPtr == NULL) break;
        else targetNode = targetNode->nextPtr;
    }

    return NULL;
}

uint8_t addNewNoteToGrid(uint16_t columnNum, uint8_t statusByte, uint8_t midiNoteNumber, uint8_t midiVelocity, uint8_t durationInSteps, bool autoAddNoteOff)
{
    uint8_t err = 0;

    //Each row represents one of the possible 128 midi notes,
    //Each column of the sequencer represents a unit of step-time

    //Step-time is the smallest unit of time (note duration) possible (expressed in 'midi ticks')
    //for the current project, and is given by: ((sequencer_ppqn*4) / sequencer_quantization)

    //The midi tick rate is determined by: 60000 / (BPM * PPQN) (in milliseconds)

    if(getPointerToEventNodeIfExists(statusByte, columnNum, midiNoteNumber) != NULL)
    {
        //For each grid co-ordinate there may be multiple nodes/events
        //but events with duplicate midiStatusBytes are NOT ALLOWED.
        assert(0);
    }
    
    if(g_GridData.gridLinkedListHeadPtrs[midiNoteNumber] != NULL)
    {
        //This linked list already has at least one node
        if(columnNum < g_GridData.gridLinkedListTailPtrs[midiNoteNumber]->column)
        {
            err |= insertNewGridData(columnNum, statusByte, midiNoteNumber, midiVelocity, durationInSteps, autoAddNoteOff);
        }
        else if(columnNum >= g_GridData.gridLinkedListTailPtrs[midiNoteNumber]->column)
        {

            err |= appendNewGridData(columnNum, statusByte, midiNoteNumber, midiVelocity, durationInSteps, autoAddNoteOff);
        }
    }
    else
    {
        //This linked list has no previously existing nodes
        err |= appendNewGridData(columnNum, statusByte, midiNoteNumber, midiVelocity, durationInSteps, autoAddNoteOff);
    }

    return err;
}

static uint8_t appendNewGridData(uint16_t columnNum, uint8_t statusByte, uint8_t midiNoteNumber, uint8_t midiVelocity, uint8_t durationInSteps, bool autoAddNoteOff)
{
    assert(midiNoteNumber < TOTAL_MIDI_NOTES);

    SequencerGridItem_t * newEventNodePtr = NULL;


    if(g_GridData.gridLinkedListHeadPtrs[midiNoteNumber] == NULL)
    {
        //--- We get here if the linked list has no existing nodes ---//

        newEventNodePtr = createNewEventNode(statusByte, columnNum, midiNoteNumber, midiVelocity, rgb_green);
        assert(newEventNodePtr != NULL);
        managePointersAndAppendNewEventNodeIntoLinkedList(newEventNodePtr, midiNoteNumber);
    }
    else
    {
        //--- This linked list DOES have existing nodes ---//

        newEventNodePtr = createNewEventNode(statusByte, columnNum, midiNoteNumber, midiVelocity, rgb_green);
        assert(newEventNodePtr != NULL);
        managePointersAndAppendNewEventNodeIntoLinkedList(newEventNodePtr, midiNoteNumber);
    }

    //Update a record of the total columns in the project
    if(columnNum > g_GridData.totalGridColumns) g_GridData.totalGridColumns = columnNum;

    if(autoAddNoteOff)
    {
        //--- Auto add corresponding Note-off event ---//

        statusByte = CLEAR_UPPER_NIBBLE(statusByte); //isolate the channel number
        statusByte |= MIDI_NOTE_OFF_MSG; //Set the upper nibble to note-off opcode
        newEventNodePtr =  createNewEventNode(statusByte, (columnNum + durationInSteps), midiNoteNumber, midiVelocity, rgb_off);
        assert(newEventNodePtr != NULL);
        managePointersAndAppendNewEventNodeIntoLinkedList(newEventNodePtr, midiNoteNumber);

        //Update a record of the total columns in the project
        if((columnNum + durationInSteps) > g_GridData.totalGridColumns) g_GridData.totalGridColumns = columnNum + durationInSteps;
    }


    return 0; 
}


static uint8_t insertNewGridData(uint16_t columnNum, uint8_t statusByte, uint8_t midiNoteNumber, 
                                        uint8_t midiVelocity, uint8_t durationInSteps, bool autoAddNoteOff)
{
    //This newly inputed grid entry occurs BEFORE the current last
    //item in the linked list for the current row/note, we therefore need
    //to find the correct location in the linked list and manually insert,
    //ensuring that all pointers are updated correctly

    assert(midiNoteNumber < TOTAL_MIDI_NOTES);
    assert(g_GridData.gridLinkedListTailPtrs[midiNoteNumber] != NULL);

    SequencerGridItem_t * tempPtr = NULL;
    SequencerGridItem_t * newEventNodePtr = NULL;

    tempPtr = g_GridData.gridLinkedListTailPtrs[midiNoteNumber];

    if(doesThisGridCoordinateFallWithinAnExistingNoteDuration(columnNum, midiNoteNumber))
    {
        ESP_LOGI(LOG_TAG, "Error: Cant place note within existing notes duration");
        return 1;
    }


    while(tempPtr != NULL)
    {
        //ADD TIMEOUT
        if(tempPtr->column <= columnNum) break;
        if(tempPtr->prevPtr == NULL)
        {
            tempPtr = NULL;
            break;
        } 
        tempPtr = tempPtr->prevPtr;
    }


    if(tempPtr ==  NULL)
    {
        //We get here if this new event node needs to be inserted at the list head 
        //this means that the global list head ptr array will also need updating
        newEventNodePtr = createNewEventNode(statusByte, columnNum, midiNoteNumber, midiVelocity, rgb_green);
        assert(newEventNodePtr != NULL);
        managePointersAndInsertNewEventNodeIntoLinkedList(newEventNodePtr, g_GridData.gridLinkedListHeadPtrs[midiNoteNumber], true);
        tempPtr = newEventNodePtr;
    } 
    else
    {
        //We get here if the new event node needs to be inserted between two existing nodes
        newEventNodePtr = createNewEventNode(statusByte, columnNum, midiNoteNumber, midiVelocity, rgb_green);
        assert(newEventNodePtr != NULL);
        managePointersAndInsertNewEventNodeIntoLinkedList(newEventNodePtr, tempPtr, false);
        tempPtr = newEventNodePtr;
    }


    if(autoAddNoteOff)
    {
        //Add corresponding note-off event - which needs to be inserted between two existing nodes
        statusByte = CLEAR_UPPER_NIBBLE(statusByte); //isolate the channel number
        statusByte |= MIDI_NOTE_OFF_MSG; //Set the upper nibble to note-off opcode
        newEventNodePtr = createNewEventNode(statusByte, (columnNum + durationInSteps), midiNoteNumber, midiVelocity, rgb_off);
        assert(newEventNodePtr != NULL);
        managePointersAndInsertNewEventNodeIntoLinkedList(newEventNodePtr, tempPtr, false);
    }

    return 0;
}


static void freePreviousGridLinkedListHeadPtrs(void)
{
    SequencerGridItem_t * nodePtr    = NULL;
    SequencerGridItem_t * deletePtr  = NULL;

    for(uint8_t a = 0; a < TOTAL_MIDI_NOTES; ++a)
    {   
        if(g_GridData.gridLinkedListHeadPtrs[a] != NULL)
        {
            nodePtr = g_GridData.gridLinkedListHeadPtrs[a];
            g_GridData.gridLinkedListHeadPtrs[a] = NULL;

            freeNextGridEventNode: //--- TIGHT LOOP ---//
            //ADD GETOUT
            deletePtr = nodePtr;
            if(nodePtr->nextPtr != NULL)
            {
                nodePtr = nodePtr->nextPtr;
                free(deletePtr);
                goto freeNextGridEventNode; //---TIGHT LOOP ---//
            } 
            else //free final node
            {
                free(deletePtr);
            }

            vTaskDelay(1); //SORT OUT THIS!

        }

        g_GridData.gridLinkedListTailPtrs[a] = NULL;
    }
}




static bool doesThisGridCoordinateFallWithinAnExistingNoteDuration(uint16_t columnNum, uint8_t midiNoteNum)
{
    //Notes on the grid can have multiple step durations, so we need
    //to make sure not to place a new note-on note-off pair such that 
    //it overlaps an existing note duration on the same row.
    //This function returns TRUE if the target coordinate falls
    //within an existing note duration on the same row, else false

    assert(g_GridData.gridLinkedListTailPtrs[midiNoteNum]->prevPtr != NULL);

    uint16_t nextCol = columnNum;
    uint16_t prevColumn = columnNum;

    //If a note-off exists at the target we know the previous note just ended
    //so the target coordinate does NOT fall within an existing note duration
    if(getPointerToEventNodeIfExists(MIDI_NOTE_OFF_MSG, columnNum, midiNoteNum) != NULL) return false;
    if(getPointerToEventNodeIfExists(MIDI_NOTE_ON_MSG, columnNum, midiNoteNum) != NULL) return true;

    //Now we need to start checking adjacent event nodes
    //on either side of the target coordinate, keep spreading
    //out either side of target until process complete
    keepSearchingForNoteDuration:
    if(prevColumn > 0)
    {
        --prevColumn;
        if(getPointerToEventNodeIfExists(MIDI_NOTE_ON_MSG, prevColumn, midiNoteNum) != NULL) return true;
        else if(getPointerToEventNodeIfExists(MIDI_NOTE_OFF_MSG, prevColumn, midiNoteNum) != NULL) return false;
    }

    if(nextCol < g_GridData.totalGridColumns)
    {
        ++nextCol;
        if(getPointerToEventNodeIfExists(MIDI_NOTE_ON_MSG, nextCol, midiNoteNum) != NULL) return false;
        else if(getPointerToEventNodeIfExists(MIDI_NOTE_OFF_MSG, nextCol, midiNoteNum) != NULL) return true;
    }
    
    if((prevColumn == 0) && (nextCol == g_GridData.totalGridColumns))
    {
        //Unexpected behaviour
        ESP_LOGE(LOG_TAG, "While working with coordinate: column: %d, row: %d", columnNum, midiNoteNum);
        assert(0);
    }
    else goto keepSearchingForNoteDuration;


    return false;
}


void printAllLinkedListEventNodesFromBase(uint16_t midiNoteNum)
{
    SequencerGridItem_t * tempPtr = NULL;
    uint16_t nodeCount = 0;

    if(g_GridData.gridLinkedListHeadPtrs[midiNoteNum] == NULL) return;

    tempPtr = g_GridData.gridLinkedListHeadPtrs[midiNoteNum];

    while(1)
    {
        //ADD GETOUT
        ++nodeCount;
        ESP_LOGI(LOG_TAG, "\n");
        ESP_LOGI(LOG_TAG, "Event node position in list: %d", nodeCount);
        ESP_LOGI(LOG_TAG, "Event status: %0x", tempPtr->statusByte);
        ESP_LOGI(LOG_TAG, "DeltaTime: %ld", tempPtr->deltaTime);
        ESP_LOGI(LOG_TAG, "Column: %d", tempPtr->column);
        ESP_LOGI(LOG_TAG, "\n");
        if(tempPtr->nextPtr == NULL) break;
        else tempPtr = tempPtr->nextPtr;
    }
}

static SequencerGridItem_t * getPointerToCorespondingNoteOffEventNode(SequencerGridItem_t * baseNodePtr)
{
    //This function seaches a rows linked list in forward direction 
    //from a supplied node ptr for a corresponding note-off event. 
    
    //If a note-on event or end of list is found before the corresponding note-off NULL is returned
    //If the search hasMultipleEventsAtSameGridCoordinates the end of the list without finding the corresponding note-off NULL is returned
    //If a corresponding note-off is found, a pointer to it is returned

    SequencerGridItem_t * tempPtr = NULL;

    if(baseNodePtr == NULL) return NULL;
    if(baseNodePtr->nextPtr == NULL) return NULL;

    tempPtr = baseNodePtr->nextPtr;

    while(tempPtr != NULL)
    {
        if(CLEAR_LOWER_NIBBLE(tempPtr->statusByte) == MIDI_NOTE_OFF_MSG) return tempPtr;
        else if(CLEAR_LOWER_NIBBLE(tempPtr->statusByte) == MIDI_NOTE_ON_MSG) return NULL;
        if(tempPtr->nextPtr == NULL) break;
        tempPtr = tempPtr->nextPtr;
    }

    return NULL;
}


static void managePointersAndAppendNewEventNodeIntoLinkedList(SequencerGridItem_t * newEventNodePtr, uint8_t listIdx)
{
    assert(newEventNodePtr != NULL);

    if(g_GridData.gridLinkedListHeadPtrs[listIdx] == NULL)
    {
        //Appending the first node to an empty list
        g_GridData.gridLinkedListHeadPtrs[listIdx] = newEventNodePtr;
        g_GridData.gridLinkedListTailPtrs[listIdx] = newEventNodePtr;
        newEventNodePtr->nextPtr = NULL;
        newEventNodePtr->nextPtr = NULL;
    }
    else
    {
        if(g_GridData.gridLinkedListTailPtrs[listIdx] == NULL)
        {
            //Unexpected NULL ptr detected whilst
            //attempting to append linked list node
            assert(0);
        }
        g_GridData.gridLinkedListTailPtrs[listIdx]->nextPtr = newEventNodePtr;
        newEventNodePtr->nextPtr = NULL;
        newEventNodePtr->prevPtr = g_GridData.gridLinkedListTailPtrs[listIdx];
        g_GridData.gridLinkedListTailPtrs[listIdx] = newEventNodePtr;
    }
}

static void managePointersAndInsertNewEventNodeIntoLinkedList(SequencerGridItem_t * newEventNodePtr, SequencerGridItem_t * insertLocationPtr, bool isLocationListHeadPtr)
{
    assert(newEventNodePtr != NULL);
    assert(insertLocationPtr != NULL);

    if(isLocationListHeadPtr) 
    {
        //Insertion location is the current head of the linked list
        //That means we need to update the global list head ptr array
        for(uint8_t idx = 0; idx <= TOTAL_MIDI_NOTES; ++idx)
        {
            if(idx == TOTAL_MIDI_NOTES)
            {
                //Shouldnt ever get here under normal operation.
                //If we got here we iterated through all the 
                //list header pointers and found no matches
                assert(0);
            }
            else
            {
                //If we've found the target list head pointer
                if(g_GridData.gridLinkedListHeadPtrs[idx] == insertLocationPtr)
                {
                    //Update global array of list head pointers
                    g_GridData.gridLinkedListHeadPtrs[idx] = newEventNodePtr; 
                    break;
                }
            }
        }

        //Update pointers as required in 
        //order to prepend the new event 
        //to linked list.. 
        newEventNodePtr->nextPtr = insertLocationPtr;
        newEventNodePtr->prevPtr = NULL;    
        insertLocationPtr->prevPtr = newEventNodePtr;
    }
    else
    { 
        //Insertion location is within body of linked list (where the 
        //insert location has existing event nodes adjacent on both 'sides')
        //A NULL nextPtr here indicates a system fault
        assert(insertLocationPtr->nextPtr != NULL);

        newEventNodePtr->nextPtr = insertLocationPtr->nextPtr;
        newEventNodePtr->prevPtr = insertLocationPtr;    
        insertLocationPtr->nextPtr->prevPtr = newEventNodePtr;
        insertLocationPtr->nextPtr = newEventNodePtr;
    }
}


static uint8_t generateDeltaTimesForCurrentGrid(void)
{
    //This function will assign midi delta-times to all event nodes
    //currenly on the grid - its much easier to process the whole grid
    //before playback or file save than to construct and edit delta-times
    //on the fly and grid events are added.

    uint16_t previousColumn = 0;
    SequencerGridItem_t * itemPtr = NULL;

    for(uint16_t currentTargetColumn = 0; currentTargetColumn <= g_GridData.totalGridColumns; ++currentTargetColumn)
    {
        //---- ADD CHECK TO SEE IF THIS COLUMN IS ACTIVE (has any events) - SKIP TO NEXT ITERATION IF NOT ACTIVE ----//

        for(uint8_t currentMidiNote = 0; currentMidiNote < TOTAL_MIDI_NOTES; ++currentMidiNote) 
        {   
            if(g_GridData.gridLinkedListHeadPtrs[currentMidiNote] != NULL)  //skip if nothing allocated
            {
                itemPtr = g_GridData.gridLinkedListHeadPtrs[currentMidiNote];

                if(itemPtr->column < currentTargetColumn)
                {
                    //Search currentMidiNote linked list for event column that matches target
                    while(itemPtr->column < currentTargetColumn)
                    {
                        if(itemPtr->nextPtr == NULL) goto lookForNext;
                        itemPtr = itemPtr->nextPtr;
                    }
                    //Check result
                    if(itemPtr->column == currentTargetColumn)
                    {
                        itemPtr->deltaTime = (itemPtr->column - previousColumn) * ((g_GridData.sequencerPPQN * NUM_QUATERS_IN_WHOLE_NOTE) / g_GridData.projectQuantization);
                        previousColumn = currentTargetColumn;
                        goto lookForNext;
                    }
                }
                else if(itemPtr->column == currentTargetColumn)
                {
                    //First node in currentMidiNote linked list is event with column that matches target
                    itemPtr->deltaTime = (itemPtr->column - previousColumn) * ((g_GridData.sequencerPPQN * NUM_QUATERS_IN_WHOLE_NOTE) / g_GridData.projectQuantization);
                    previousColumn = currentTargetColumn;
                    goto lookForNext;
                }

                lookForNext:
            }
        }
    }

    return 0;
}


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

    //RETURNS: The size of the newly generated midi file in bytes.

    //TODO: Add checks to make sure file buffer size not exceeded!

    register uint32_t buffer;
    uint32_t deltaTime;
    SequencerGridItem_t * tempGridItemPtr = NULL;
    uint32_t trackChunkSizeInBytes;
    bool hasMultipleEventsAtSameGridCoordinate = false;

    assert(midiFileBufferPtr != NULL);
    assert(g_GridData.totalGridColumns > 0);

    memset(midiFileBufferPtr, 0, bufferSize);
    generateEmptyMidiFile(midiFileBufferPtr, g_GridData.sequencerPPQN, 120);
    generateDeltaTimesForCurrentGrid();

    const uint8_t * trackChunkBasePtr = (midiFileBufferPtr + MIDI_FILE_TRACK_HEADER_OFFSET);
    midiFileBufferPtr += MIDI_FILE_MIDI_EVENTS_OFFSET;

    for(uint16_t currentTargetColumn = 0; currentTargetColumn <= g_GridData.totalGridColumns; ++currentTargetColumn)
    {
        hasMultipleEventsAtSameGridCoordinate = false;

        for(uint8_t currentRow = 0; currentRow < TOTAL_MIDI_NOTES; ++currentRow)
        {   

            if(g_GridData.gridLinkedListHeadPtrs[currentRow] != NULL)  //skip row if nothing allocated
            {

                tempGridItemPtr = g_GridData.gridLinkedListHeadPtrs[currentRow];

                multipleNodesWithSameColumnRow:

                //Search the current row for any events which
                //exists within the target grid column
                while(tempGridItemPtr->column < currentTargetColumn)
                {
                    if(tempGridItemPtr->nextPtr == NULL) goto next;
                    tempGridItemPtr = tempGridItemPtr->nextPtr;
                }

                if(tempGridItemPtr->column != currentTargetColumn) goto next;

                //ESP_LOGI(LOG_TAG, "Column num: %d", currentTargetColumn);
                //ESP_LOGI(LOG_TAG, "Item Colum: %d", tempGridItemPtr->column);
                //ESP_LOGI(LOG_TAG, "Note num: %0x", currentRow);

                //If we reached here, that means that the current
                //target row x column grid co-orintate exists
                //The target grid event will now be added to the 
                //midi file track chunk currenrly being constructed

                //NOTE: There can be multiple events/nodes with the same column x row
                //When multiple events exist at the same grid coordinate only the deltatimes
                //of all but the first event written to file will be forced to zero. 
                if(hasMultipleEventsAtSameGridCoordinate) deltaTime = 0;
                else deltaTime = tempGridItemPtr->deltaTime;

                //ADD CHECKS TO MAKE SURE MAXIMUM FILE SIZE NOT POSSIBLE TO EXCEED!

                if(deltaTime > MAX_DELTA_TIME_BYTE_VALUE)
                {
                    //The 'buffer' will be used as a lifo byte buffer,
                    //it will hold the encoded delta-time bytes which
                    //are generated below. 

                    buffer = CLEAR_MSBIT_IN_BYTE(deltaTime);
                    while(deltaTime >>= (NUM_BITS_IN_BYTE - 1))
                    {
                        buffer <<= NUM_BITS_IN_BYTE;
                        buffer |= (CLEAR_MSBIT_IN_BYTE(deltaTime) | (1 << (NUM_BITS_IN_BYTE - 1)));
                    }

                    //The LSB of 'buffer' is now the MSB of
                    //the encoded delta-time bytes as they
                    //will appear in the file written below.
                    uint8_t rawDeltaTimeByteCount = 0;
                    while(rawDeltaTimeByteCount < MIDI_FILE_MAX_DELTA_TIME_NUM_BYTES)
                    {
                        *midiFileBufferPtr = (uint8_t)buffer; //Write encoded delta-time byte to file
                        ++rawDeltaTimeByteCount;
                        ++midiFileBufferPtr;
                        if (GET_MSBIT_IN_BYTE(buffer)) buffer >>= NUM_BITS_IN_BYTE;
                        else break;
                    }
                }
                else
                {
                    *midiFileBufferPtr = (uint8_t)deltaTime;
                    ++midiFileBufferPtr;
                }

                *midiFileBufferPtr = tempGridItemPtr->statusByte; //status
                ++midiFileBufferPtr;
                *midiFileBufferPtr = tempGridItemPtr->dataBytes[0]; //note num
                ++midiFileBufferPtr;
                *midiFileBufferPtr = tempGridItemPtr->dataBytes[1]; //velocity
                ++midiFileBufferPtr;

                hasMultipleEventsAtSameGridCoordinate = true;

                if(tempGridItemPtr->nextPtr != NULL)
                {
                    if(tempGridItemPtr->nextPtr->column == tempGridItemPtr->column)
                    {
                        //There are multiple events/nodes which 
                        //share the same grid co-ordinates
                        tempGridItemPtr = tempGridItemPtr->nextPtr;
                        goto multipleNodesWithSameColumnRow;
                    }
                }
            }
            next:
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

    //Determine the total size of the midi 
    //track data NOT including the track header.
    //The calculated value will then need to be 
    //written to the 'size' field of the track header.
    trackChunkSizeInBytes = midiFileBufferPtr - trackChunkBasePtr;
    trackChunkSizeInBytes -= (MIDI_TRACK_HEADER_NUM_BYTES + MIDI_FILE_TRACK_SIZE_FIELD_NUM_BYTES);

    //Set the file buffer pointer the the base of
    //the four byte 'size' field within the track header
    midiFileBufferPtr = (trackChunkBasePtr + MIDI_TRACK_HEADER_NUM_BYTES);

    //Now write the four byte size field of the track header to file buffer
    for(int8_t a = (MIDI_FILE_TRACK_SIZE_FIELD_NUM_BYTES-1); a >= 0 ; --a)
    {
        *midiFileBufferPtr =  (uint8_t)(trackChunkSizeInBytes >> (a * NUM_BITS_IN_BYTE));
        ++midiFileBufferPtr;
    }

    return (trackChunkSizeInBytes + MIDI_FILE_MIDI_EVENTS_OFFSET);
}




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

    assert(midiFileBufferPtr != NULL);
    assert(getMidiFileFormatType(midiFileBufferPtr) == MIDI_FILE_FORMAT_TYPE0);

    //Clear out any previous grid data
    freePreviousGridLinkedListHeadPtrs();
    g_GridData.totalGridColumns = 0;

    //Set the pointer to the BASE of the 
    //first midi event before processing
    midiFileBufferPtr += MIDI_FILE_MIDI_EVENTS_OFFSET;

    while(1)
    {
        currentDeltaTime = processMidiFileDeltaTime(midiFileBufferPtr);
        deltaTimeNumBytes = getDeltaTimeVaraibleLengthNumBytes(currentDeltaTime);
        //Handle incrementing file ptr after reading current delta-time
        if(deltaTimeNumBytes <= MIDI_FILE_MAX_DELTA_TIME_NUM_BYTES) midiFileBufferPtr += deltaTimeNumBytes;
        else
        {
            corruptFileDetected = true;
            break;
        }

        totalColumnCount += currentDeltaTime / ((g_GridData.sequencerPPQN * NUM_QUATERS_IN_WHOLE_NOTE) / g_GridData.projectQuantization);

        statusByte = *midiFileBufferPtr;

        vTaskDelay(1);

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

            //The MIDI spec features 'running status' capability,
            //if the current event type is the event immediately
            //before it, then the new event does not need a status byte
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
                    if(midiVoiceMsgData[0] < TOTAL_MIDI_NOTES)
                    {
                        //Construct a double linked list for each row of sequencer used
                        //each linked list node is a struct containing pointers and midi data

                        addNewNoteToGrid(totalColumnCount, statusByte, midiVoiceMsgData[0], midiVoiceMsgData[1], 0, false);
                        
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
                    //Byte[1] = Program value (selects instrument)
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
            //This must be a 'running status', where subsequent
            //events of the same type may ommit the status byte
            ESP_LOGE(LOG_TAG, "Error: Running status detected - not supported");
            corruptFileDetected = true;
            break;
        }
    }

    if(corruptFileDetected)
    {
        assert(0);
    }
    else
    {
        ESP_LOGI(LOG_TAG, "midiFileToGrid SUCCESS, total columns in project: %d", totalColumnCount);
        g_GridData.totalGridColumns = ++totalColumnCount; //must account for zero base
    }
}



void updateGridLEDs(uint8_t rowOffset, uint16_t columnOffset)
{
    //This function updates all rgbs leds of the switch matrix 

    SequencerGridItem_t * tempPtr = NULL;
    SequencerGridItem_t * searchPtr = NULL;
    bool abortCurrentRow = false;
    bool runOncePerRow = false;
    uint16_t numGridSteps = 0;
    uint8_t relativeRow = 0;
    uint16_t relativeColumn = 0;
    rgbLedColour_t gridRGBCodes[48] = {rgb_off};

    //This system uses an array of linked lists, one for each of the possible 128 midi notes.
    //Each of these linked lists hold data for one row of the sequencer grid matrix data.
    //The number of columns in the sequencer grid martrix is dynamic, growing with the project.

    //The pysical hardware grid is a matrix of 48 switches, made up of 6 rows of 8 columns
    //Each switch has its own assosiated RGB led, this function controlls the led driver for the hardware grid.

    //As the physical grid is limited in size, only a 6x8 'window'
    //of the larger 128xN (N = dynamic num columns) can be displayed

    //The input arguments 'rowOffset' and 'columnOffset' and relative to 0x0 on the physical grid.

    //Example: rowOffset = 5, columnOffset = 8.
    //The physical hardware grid will be displaying rows 5 -> 10 and columns 8 -> 15.
    //The code below look look for event nodes that have grid coordinates within that
    //window, setting rgb colours as appropriate (each node has an rgbColour member)

    for(uint8_t idx = rowOffset; idx < (rowOffset + NUM_SEQUENCER_HARDWARE_ROWS + 1); ++idx)
    { 
        runOncePerRow = true; //set flag for current row

        if(g_GridData.gridLinkedListHeadPtrs[idx] != NULL)
        {
            //We get here is the linked list for the current
            //row has at least one or more event nodes.

            if(g_GridData.gridLinkedListTailPtrs[idx] == NULL)
            {
                //Shouldn't be possible under normal operation
                ESP_LOGE(LOG_TAG, "Error: Unexpected NULL ptr in 'updateGridLEDs'");
                return;
            }

            //Now we need to find out if any of the list nodes,
            //fall within the column range speicifed by 'columnOffset'

            //If TRUE this linked list may contain event nodes which
            //fall within the target window and will need to be searched
            if(g_GridData.gridLinkedListTailPtrs[idx]->column >= columnOffset)
            {
                tempPtr = g_GridData.gridLinkedListHeadPtrs[idx];

                //Now search for first event node to fall within window, if one exists
                findEventsWithinWindow:

                //If TRUE, the event node pointed at by tempPtr DOES NOT fall within the target window
                if((tempPtr->column >= columnOffset) && (tempPtr->column < columnOffset + NUM_GRID_COLUMNS_PER_ROW) == false)
                {
                    if(tempPtr->nextPtr !=  NULL)
                    {
                        //current node coordinates didnt fall within
                        //target window so move onto next node in the list
                        tempPtr = tempPtr->nextPtr;
                        goto findEventsWithinWindow;
                    }
                    else
                    {
                        //end of linked list
                        abortCurrentRow = true;
                    }
                }
                //The end of the list hasnt been reached, but we are no longer within the target window
                else if(tempPtr->column > (columnOffset + NUM_GRID_COLUMNS_PER_ROW)) abortCurrentRow = true;

                if(!abortCurrentRow)
                {
                    //We reach here if a target node has been found 
                    //to exist within the specified grid window

                    //If everything worked 'tempGridItemPtr' should now be pointing to the first
                    //item for the specified row that falls within the target grid area. There
                    //may be a single item or multiple items that fall within the target grid area

                    //remove offset to get zero base hardware column num
                    relativeColumn = tempPtr->column - columnOffset;
        
                    if((CLEAR_LOWER_NIBBLE(tempPtr->statusByte) == MIDI_NOTE_OFF_MSG) && (tempPtr->column > columnOffset) && (runOncePerRow))
                    {
                        //We only get here when the first event node in the current linked list is a note off event
                        //while note-off events are not themselves displayed, if the relativeColumn is greater than zero
                        //it means we have an existing note duration that overruns into the target window.
                        //Any grid steps that are a note duration overrun must be displayed.
                        runOncePerRow = false;
                        numGridSteps = tempPtr->column - columnOffset;

                        for(uint8_t a = 0; a <= numGridSteps; ++a)
                        {
                            //Set the colour of the grid coordinates that make up the note duration overrun
                            gridRGBCodes[0 + (relativeRow * NUM_GRID_COLUMNS_PER_ROW) + a] = rgb_green;
                        }
                    }
                    else if(CLEAR_LOWER_NIBBLE(tempPtr->statusByte) == MIDI_NOTE_ON_MSG)
                    {
                        //A note-on event has been found within the target window
                        //we must now find the duration of that note off event in steps
                        runOncePerRow = false;
                        //Search for and get pointer to corresponding note-off
                        searchPtr = getPointerToCorespondingNoteOffEventNode(tempPtr);
                        if(searchPtr != NULL) 
                        {
                            //We get here if the corresponding note-off to 
                            //the current note-on event has been found to exist
                            numGridSteps = searchPtr->column - tempPtr->column;
                        }
                        else
                        {
                            //Couldnt find corresponding note off event (SHOULD NOT REACH HERE)
                            numGridSteps = 1; //illuminate the coordinate of the note-on event itself
                            ESP_LOGE(LOG_TAG, "Error: Unable to find expected corresponding note-off in 'updateGridLEDs'");
                        } 

                        for(uint8_t a = 0; a < numGridSteps; ++a)
                        {
                            //Set the colour of the grid coordinates that make up the current notes duration
                            gridRGBCodes[relativeColumn + (relativeRow * NUM_GRID_COLUMNS_PER_ROW) + a] = rgb_green;
                        }

                        tempPtr = searchPtr;
                    }

                    if(tempPtr->nextPtr !=  NULL)
                    {
                        //Still more nodes to process
                        tempPtr = tempPtr->nextPtr;
                        goto findEventsWithinWindow;
                    }
                }
            }
        }
        abortCurrentRow = false; //reset flag for next row
        ++relativeRow; //Increment zero offset hardware row
    }

    ledDrivers_writeEntireGrid(gridRGBCodes);
}