#include "esp_log.h"
#include "include/system.h"
#include "include/systemEvents.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "guiMenu.h"
#include "sequencerGrid.h"
#include "rotaryEncoders.h"
#include "bleCentClient.h"
#include "switchMatrix.h"
#include "ipsDisplay.h"
#include "fileSys.h"
#include "midiHelper.h"


#define LOG_TAG "systemComponent"
#define MATRIX_SCANNER_TASK_STACK_SIZE 4096     //Still need to tune this stack size
#define BLE_CLIENT_TASK_STACK_SIZE 8192         //Still need to tune this stack size
#define FILE_BUFFER_SIZE 1024 * 1024            //1MB (8MB available on this part)


//---- Private ----//
static void initRTOSTasks(void);


//This type will act as a container for all 
//settings relating to the current sequencer project
typedef struct
{
    char fileName[MAX_FILENAME_CHARS + 1]; // plus one for termination
    uint8_t projectTempo;
    uint8_t quantization;
    // midi event cache
    uint8_t midiStatus;
    uint8_t midiNote;
    uint8_t midiVelocity;
    // sequencer grid
    uint32_t midiFileSizeBytes;
} ProjectParameters_t;


//Statically allocate all memory and objects 
//required for the switch matrix RTOS task
static TaskHandle_t g_SwitchMatrixTaskHandle = NULL;    
static StaticTask_t g_SwitchMatrixTaskBuffer; //Private high-level RTOS task data
static StackType_t g_SwitchMatrixTaskStack[MATRIX_SCANNER_TASK_STACK_SIZE];

//Statically allocate all memory and objects 
//required for the BLE gatt client RTOS task
static TaskHandle_t g_BleClientTaskHandle = NULL;
static StaticTask_t g_BleClientTaskBuffer;  //Private high-level RTOS task data
static StackType_t g_BleClientTaskStack[BLE_CLIENT_TASK_STACK_SIZE];

//This pointer is allocated memory from PSRAM at system startup. 
//The allocated memory is used as the systems midi file buffer,
//holding the midi file data relating to the current project.
uint8_t * g_midiFileBufferPtr = NULL;





//---- Public
void system_EntryPoint(void)
{
    //The system module manages all sub-modules which make up the
    //system as a whole, the idea being that sub-module co-dependancies
    //are avoided/reduced to absolute minimum.

    MenuEventData_t RxMenuEvent;
    ProjectParameters_t ProjectSettings;
    SwitchMatrixQueueItem_t SwitchMatrixQueueItem;
    uint8_t menuInputBuffer;

    bool isGridActive = false;
    bool hasEncoderInput = false;
    bool hasGridInput = false;

    //Make sure all system objects are cleared
    memset(&RxMenuEvent, 0, sizeof(MenuEventData_t));
    memset(&ProjectSettings, 0, sizeof(ProjectParameters_t));
    memset(&SwitchMatrixQueueItem, 0, sizeof(SwitchMatrixQueueItem_t));

    //Allocate midi file buffer from PSRAM
    g_midiFileBufferPtr = heap_caps_malloc(FILE_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    assert(g_midiFileBufferPtr != NULL);
    //Keep a read-only record of the file cache base address
    const uint8_t * const midiFileBufferBASEPtr = g_midiFileBufferPtr;

    //Initialize and mount the file system
    FileSysPublicData_t FileSysInfo = fileSys_init();
    assert(*FileSysInfo.isPartitionMountedPtr == true);
    
    //Initialize sub-modules

    initIPSDisplayDriver();
    rotaryEncoders_init();
    guiMenu_init(FileSysInfo.filenamesPtr, FileSysInfo.numFilesOnPartitionPtr);
    ledDrivers_init();
    ledDrivers_init();
    resetSequencerGrid(MIDI_SEQUENCER_PPQ, 4);
    //initRTOSTasks();

    bool hasLoadedGrid = false;
    uint8_t gridroffset = 0x34;
    int16_t gridcoffset = 0;

    while (1)
    {
        if (uxQueueMessagesWaiting(g_EncodersQueueHandle))
        {
            xQueueReceive(g_EncodersQueueHandle, &menuInputBuffer, 0);

            if(hasLoadedGrid)
            {
                if(menuInputBuffer == encoder1_cw)
                {
                    gridroffset++;
                }
                else if(menuInputBuffer == encoder1_ccw)
                {
                    gridroffset--;
                }

                if(menuInputBuffer == encoder0_cw)
                {
                    gridcoffset++;
                }
                else if(menuInputBuffer == encoder0_ccw)
                {
                    gridcoffset--;
                    if(gridcoffset < 0) gridcoffset = 0;
                }

                updateGridLEDs(gridroffset, gridcoffset);
            }

            hasEncoderInput = true;
        }
/*
        if (uxQueueMessagesWaiting(g_SwitchMatrixQueueHandle) && isGridActive)
        {
            xQueueReceive(g_SwitchMatrixQueueHandle, &SwitchMatrixQueueItem, 0);
            hasGridInput = true;
        }
*/
        if (hasEncoderInput)
        {
            hasEncoderInput = false;
            RxMenuEvent = guiMenu_interface(menuInputBuffer);
        }

        switch (RxMenuEvent.eventOpcode)
        {
            case 0: //--- No action
                break;

            case 1: //--- New Project: Create new midi template file
                ESP_LOGI(LOG_TAG, "RxMenuEvent.eventOpcode == 1 (create new project midi template file)");
                /*
                if (createNewSequencerProject(midiFileBufferBASEPtr, ProjectSettings.fileName, ProjectSettings.projectTempo, ProjectSettings.quantization) != 0)
                {
                    ESP_LOGE(LOG_TAG, "Error: failed to create new sequencer project");
                }
                */
                break;

            case 2: //--- Save file
                ESP_LOGI(LOG_TAG, "RxMenuEvent.eventOpcode == 2 (save current file)");
                break;

            case 3: //--- Close file
                ESP_LOGI(LOG_TAG, "RxMenuEvent.eventOpcode == 3 (close current file)");
                memset(&ProjectSettings, 0, sizeof(ProjectSettings));
                break;

            case 4: //--- Open file
                ESP_LOGI(LOG_TAG, "RxMenuEvent.eventOpcode == 4 (open file)");
                break;

            case 5: //--- Set project tempo
                ESP_LOGI(LOG_TAG, "RxMenuEvent.eventOpcode == 5 (set project tempo)");
                if (RxMenuEvent.eventData != NULL)
                {
                    if (*(uint8_t *)RxMenuEvent.eventData <= 240)
                        ProjectSettings.projectTempo = *(uint8_t *)RxMenuEvent.eventData;
                    else
                        ESP_LOGE(LOG_TAG, "Error: Selected tempo out of bounds");
                }
                else
                    ESP_LOGE(LOG_TAG, "Error: Null pointer detected while attempting to set tempo");
                break;

            case 6: //--- Set project name
                ESP_LOGI(LOG_TAG, "RxMenuEvent.eventOpcode == 6 (set project name)");
                if (RxMenuEvent.eventData != NULL)
                {
                    if (strlen(RxMenuEvent.eventData) <= MAX_FILENAME_CHARS)
                        strcpy(ProjectSettings.fileName, RxMenuEvent.eventData);
                    else
                        ESP_LOGE(LOG_TAG, "Error: Project name length out of bounds");
                }
                else
                    ESP_LOGE(LOG_TAG, "Error: Null pointer detected while attempting to set project name");
                break;

            case 7: //--- Set project quantization
                ESP_LOGI(LOG_TAG, "RxMenuEvent.eventOpcode == 7 (set project quantization)");
                if (RxMenuEvent.eventData != NULL)
                {
                    ProjectSettings.quantization = *(uint8_t *)RxMenuEvent.eventData;
                }
                else
                    ESP_LOGE(LOG_TAG, "Error: Null pointer detected while attempting to set project quantization");
                break;

            default:
                ESP_LOGE(LOG_TAG, "Unrecognised RxMenuEvent, ignoring");
                break;
        }

        if ((RxMenuEvent.eventOpcode == 1 || RxMenuEvent.eventOpcode == 4) && hasLoadedGrid == false)
        {
            // When starting a new project, or opening an existing project the
            // sequencer grid needs to be reset to its initial (blank) state

            //addNewMidiEventToGrid(0, 0x90, 0x37, 60, 4, true);
            addNewMidiEventToGrid(6, 0x90, 0x37, 60, 2);
            vTaskDelay(1);
            addNewMidiEventToGrid(4, 0x90, 0x37, 60, 1);
            vTaskDelay(1);
            //printAllLinkedListEventNodesFromBase(0x37);
            addNewMidiEventToGrid(5, 0x90, 0x37, 60, 1);
            vTaskDelay(1);

            addNewMidiEventToGrid(0, 0x90, 0x34, 60, 1);
            addNewMidiEventToGrid(7, 0x90, 0x34, 60, 1);

            addNewMidiEventToGrid(0, 0x90, 0x39, 60, 1);
            addNewMidiEventToGrid(7, 0x90, 0x39, 60, 1);
            vTaskDelay(1);


            //addNewMidiEventToGrid(4, 0x90, 0x37, 60, 1, true); //shouldnt appear   
            //addNewMidiEventToGrid(2, 0x90, 0x37, 60, 3, true); //shouldnt appear   

            ESP_LOGI(LOG_TAG, "Updating LEDS");
            updateGridLEDs(0x34, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));

            ProjectSettings.midiFileSizeBytes = gridDataToMidiFile(midiFileBufferBASEPtr, FILE_BUFFER_SIZE);

            printEntireMidiFileContentToConsole(midiFileBufferBASEPtr, ProjectSettings.midiFileSizeBytes);

            fileSys_writeFile(ProjectSettings.fileName, midiFileBufferBASEPtr, ProjectSettings.midiFileSizeBytes, true);

            ProjectSettings.midiFileSizeBytes = fileSys_readFile(ProjectSettings.fileName, midiFileBufferBASEPtr, 0, true);

            printEntireMidiFileContentToConsole(midiFileBufferBASEPtr, ProjectSettings.midiFileSizeBytes);

            midiFileToGrid(midiFileBufferBASEPtr, FILE_BUFFER_SIZE);

            ESP_LOGI(LOG_TAG, "Update sequencer grid LEDS");

            updateGridLEDs(0x34, 0);
            hasLoadedGrid = true;
        }

        RxMenuEvent.eventOpcode = 0;
        RxMenuEvent.eventData = NULL;

        vTaskDelay(1); // Smash idle
    }
}








//---- Private
static void initRTOSTasks(void)
{
    //--------------------------------------------------
    //------------ SWITCH MATRIX TASK ------------------
    //--------------------------------------------------
    g_SwitchMatrixQueueHandle = xQueueCreate(10, sizeof(SwitchMatrixQueueItem_t));
    assert(g_SwitchMatrixQueueHandle != NULL);

    g_SwitchMatrixTaskHandle = xTaskCreateStaticPinnedToCore(
        switchMatrix_TaskEntryPoint, "switchMatrixTask", MATRIX_SCANNER_TASK_STACK_SIZE, NULL, 1, g_SwitchMatrixTaskStack, &g_SwitchMatrixTaskBuffer, 0);

    //---------------------------------------------------
    //------------- BLUETOOTH TASK SETUP ----------------
    //---------------------------------------------------
    g_HostToBleQueueHandle = xQueueCreate(10, sizeof(HostToBleQueueItem_t));
    g_BleToHostQueueHandle = xQueueCreate(10, sizeof(uint8_t));
    assert((g_HostToBleQueueHandle != NULL) && (g_BleToHostQueueHandle != NULL));

    g_BleClientTaskHandle = xTaskCreateStaticPinnedToCore(bleCentAPI_task, "bleClientTask", BLE_CLIENT_TASK_STACK_SIZE,
                                                             NULL, 1, g_BleClientTaskStack, &g_BleClientTaskBuffer, 1);
}