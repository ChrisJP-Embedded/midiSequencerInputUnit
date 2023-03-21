#include "esp_log.h"
#include "include/system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "guiMenu.h"
#include "switchMatrix.h"
#include "rotaryEncoders.h"
#include "bleCentClient.h"
#include "ipsDisplay.h"
#include "fileSys.h"
#include "midiHelper.h"
#include "gridManager/gridManager.h"

#define LOG_TAG "systemComponent"

//TODO: Tune stack sizes
#define GRID_MANAGER_TASK_STACK_SIZE    4096
#define BLE_CLIENT_TASK_STACK_SIZE      8192
#define GUI_MENU_TASK_STACK_SIZE        8192
#define MATRIX_SCANNER_TASK_STACK_SIZE  4096

#define GUI_MENU_TASK_PRIORITY          2
#define GRID_MANAGER_TASK_PRIORIRY      1
#define BLE_CLIENT_TASK_PRIORITY        1
#define FILE_BUFFER_SIZE                1024 * 1024         //1MB (8MB available on this part)


static void initRTOSTasks(void * menuParams, void * switchMatrixParams, void * bleParams);


//This type will act as a container for all 
//settings relating to the current sequencer project
typedef struct
{
    char fileName[MAX_FILENAME_CHARS + 1]; // plus one for termination
    uint8_t projectTempo;
    uint8_t quantization;
    uint8_t gridDisplayRowOffset;
    uint8_t gridDisplayColumnOffset;
} ProjectParameters;

//This pointer is allocated memory from PSRAM at system startup. 
//The allocated memory is used as the systems midi file buffer,
//holding the midi file data relating to the current project.
uint8_t * g_midiFileBufferPtr = NULL;




//GUI menu RTOS task
static TaskHandle_t g_GUIMenuTaskHandle;
static StaticTask_t g_GUIMenuTaskBuffer;
static StackType_t g_GUIMenuTaskStack[GUI_MENU_TASK_STACK_SIZE];


//BLE GATT Client RTOS task
static TaskHandle_t g_BleClientTaskHandle;
static StaticTask_t g_BleClientTaskBuffer;
static StackType_t g_BleClientTaskStack[BLE_CLIENT_TASK_STACK_SIZE];


//Switch Matrix RTOS task
static TaskHandle_t g_SwitchMatrixTaskHandle;    
static StaticTask_t g_SwitchMatrixTaskBuffer;
static StackType_t g_SwitchMatrixTaskStack[MATRIX_SCANNER_TASK_STACK_SIZE];






//---- Public
void system_EntryPoint(void)
{
    uint8_t operatingMode = 0;
    MenuQueueItem menuInputEvent;
    SwitchMatrixQueueItem swMatrixEvent;
    MidiEventParams midiEventParams;
    ProjectParameters projectParams = {0};

    bool isGridActive = false;
    bool hasEncoderInput = false;
    bool hasGridInput = false;

    //Allocate midi file buffer from PSRAM
    g_midiFileBufferPtr = heap_caps_malloc(FILE_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    assert(g_midiFileBufferPtr != NULL);
    //Keep a read-only record of the file cache base address
    const uint8_t * const midiFileBufferBASEPtr = g_midiFileBufferPtr;

    //Initialize and mount the file system
    FileSysPublicData FileSysInfo = fileSys_init();
    assert(*FileSysInfo.isPartitionMountedPtr == true);
    
    //Initialize sub-modules
    IPSDisplay_init();
    rotaryEncoders_init();
    gridManager_init();

    //Update system task priority
    vTaskPrioritySet(NULL, 1);

    //Now we want to initialize the RTOS tasks and assosiated
    //queues that make up the various system runtime processes.
    initRTOSTasks(&FileSysInfo, NULL, NULL);


    while (1)
    {
        if(xQueueReceive(g_MenuToSystemQueueHandle, &menuInputEvent, 0) == pdTRUE)
        {
            vTaskPrioritySet(NULL, 3);
            switch(menuInputEvent.eventOpcode)
            {
                //NOTE: Literals will be replaced with enumerations later

                case 1: 
                    ESP_LOGI(LOG_TAG, "Save current project");
                    //uint32_t fileSize = gridManager_gridDataToMidiFile(midiFileBufferBASEPtr, FILE_BUFFER_SIZE);
                    //fileSys_writeFile(char * c, midiFileBufferBASEPtr, fileSize, true);
                    break;

                case 2:
                    ESP_LOGI(LOG_TAG, "Updated note velocity");
                    midiEventParams.dataBytes[MIDI_VELOCITY_IDX] = menuInputEvent.payload[0];
                    gridManager_updateMidiEventParameters(midiEventParams);
                    break;

                case 3:
                    ESP_LOGI(LOG_TAG, "Updated note duration");
                    midiEventParams.durationInSteps = menuInputEvent.payload[0];
                    gridManager_updateMidiEventParameters(midiEventParams);
                    gridManager_updateGridLEDs(0x34,0);
                    break;

                case 4:
                    ESP_LOGI(LOG_TAG, "Initialize new project params");
                    //strcpy(projectParams.fileName, );
                    //projectParams.projectTempo =
                    //projectParams.quantization =
                    //projectParams.gridDisplayColumnOffset = 0;
                    //projectParams.gridDisplayRowOffset =
                    break;

                case 5:
                    ESP_LOGI(LOG_TAG, "Load project");
                    //strcpy(projectParams.fileName, );
                    //uint32_t fileSize = fileSys_readFile(projectParams.fileName, midiFileBufferBASEPtr, 0, true);
                    //gridManager_midiFileToGrid(midiFileBufferBASEPtr, fileSize);
                    break;

                case 6: 
                    ESP_LOGI(LOG_TAG, "Start playback");
                    break;

                case 7: 
                    ESP_LOGI(LOG_TAG, "Stop playback");
                    break;


                default:
                    assert(0);
                    break;
            }
            vTaskPrioritySet(NULL, 1);
        }


        if(xQueueReceive(g_SwitchMatrixQueueHandle, &swMatrixEvent, 0) == pdTRUE)
        {
            vTaskPrioritySet(NULL, 3);
            //We eneter here when the grid is active and a switch
            //within the grid has been pressed we must now retreive
            //details for the grid coordinate.
            midiEventParams = gridManager_getNoteParamsIfCoordinateFallsWithinExistingNoteDuration(swMatrixEvent.column,  (swMatrixEvent.row + 0x34), 0);

            if(midiEventParams.statusByte == 0)
            {
                //We eneter here when the midi event params retireved
                //from the grid manager show that there is no pre-existing
                //event at the grid coordinate pressed by the user.
                //We need to load default settings for the midi event
                //type and channel currently being edited.
                //NOTE: CURRENTLY ONLY SUPPORT CHANNEL 0 AND MIDI NOTE EVENTS.

                midiEventParams.gridColumn = swMatrixEvent.column;
                midiEventParams.gridRow = (swMatrixEvent.row + 0x34);
                midiEventParams.statusByte = 0x90; //sort later
                midiEventParams.durationInSteps = 1;
                midiEventParams.dataBytes[MIDI_NOTE_NUM_IDX] = (swMatrixEvent.row + 0x34);
                midiEventParams.dataBytes[MIDI_VELOCITY_IDX] = 127;
                gridManager_addNewMidiEventToGrid(midiEventParams);
                gridManager_updateGridLEDs(0x34,0);
            }

            //We need to let the menu task know that a grid coordinate
            //has been pressed and send the event params for that coord
            MenuQueueItem txMenuQueueItem = {
                .eventOpcode = 5, 
                .payload[0] = midiEventParams.statusByte,
                .payload[1] = midiEventParams.dataBytes[MIDI_NOTE_NUM_IDX],
                .payload[2] = midiEventParams.dataBytes[MIDI_VELOCITY_IDX],
                .payload[3] = midiEventParams.durationInSteps,
                .payload[4] = ((midiEventParams.stepsToNext == 0) ? 128 : (midiEventParams.stepsToNext))
            };

            //Send the coordinate parameters to menu to be displayed
            xQueueSend(g_SystemToMenuQueueHandle, &txMenuQueueItem, 0);
            vTaskPrioritySet(NULL, 1);
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }

    assert(0);
}




static void initRTOSTasks(void * menuParams, void * switchMatrixParams, void * bleParams)
{

    //--------------------------------------------------
    //---------------- MENU INTERFACE ------------------
    //--------------------------------------------------
    g_MenuToSystemQueueHandle = xQueueCreate(1, sizeof(MenuQueueItem));
    g_SystemToMenuQueueHandle = xQueueCreate(1, sizeof(MenuQueueItem));
    assert(g_MenuToSystemQueueHandle != NULL);

    g_GUIMenuTaskHandle = xTaskCreateStaticPinnedToCore(guiMenu_entryPoint, "guiMenu", GUI_MENU_TASK_STACK_SIZE,
                                                        menuParams, 2, g_GUIMenuTaskStack, &g_GUIMenuTaskBuffer, 0);

    //--------------------------------------------------
    //------------- SWITCH MATRIX TASK -----------------
    //--------------------------------------------------
    g_SwitchMatrixQueueHandle = xQueueCreate(SWITCH_MATRIX_QUEUE_NUM_ITEMS, sizeof(SwitchMatrixQueueItem));
    assert(g_SwitchMatrixQueueHandle != NULL);

    g_SwitchMatrixTaskHandle = xTaskCreateStaticPinnedToCore(switchMatrix_TaskEntryPoint, "switchMatrixTask", MATRIX_SCANNER_TASK_STACK_SIZE,
                                                            switchMatrixParams, 1, g_SwitchMatrixTaskStack, &g_SwitchMatrixTaskBuffer, 0);

    //vTaskSuspend(g_SwitchMatrixTaskHandle);

    //--------------------------------------------------
    //-------------- BLUETOOTH TASK --------------------
    //--------------------------------------------------
    g_HostToBleQueueHandle = xQueueCreate(10, sizeof(HostToBleQueueItem));
    g_BleToHostQueueHandle = xQueueCreate(10, sizeof(uint8_t));
    assert(g_HostToBleQueueHandle != NULL); 
    assert(g_BleToHostQueueHandle != NULL);

    g_BleClientTaskHandle = xTaskCreateStaticPinnedToCore(bleCentAPI_task, "bleClientTask", BLE_CLIENT_TASK_STACK_SIZE,
                                                        bleParams, 1, g_BleClientTaskStack, &g_BleClientTaskBuffer, 1);
    vTaskSuspend(g_BleClientTaskHandle);
}