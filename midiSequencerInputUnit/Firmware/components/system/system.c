#include "esp_log.h"
#include "include/system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "guiMenu.h"
#include "gridManager/gridManager.h"
#include "switchMatrix.h"
#include "rotaryEncoders.h"
#include "bleCentClient.h"
#include "ipsDisplay.h"
#include "fileSys.h"
#include "midiHelper.h"

#define LOG_TAG "systemComponent"

//TODO: Tune stack sizes
#define GRID_MANAGER_TASK_STACK_SIZE    4096
#define BLE_CLIENT_TASK_STACK_SIZE      8192
#define GUI_MENU_TASK_STACK_SIZE        8192
#define MATRIX_SCANNER_TASK_STACK_SIZE  4096

#define GUI_MENU_TASK_PRIORITY          2
#define GRID_MANAGER_TASK_PRIORIRY      1
#define BLE_CLIENT_TASK_PRIORITY        1

#define FILE_BUFFER_SIZE 1024 * 1024         //1MB (8MB available on this part)


static void initRTOSTasks(void * menuParams, void * switchMatrixParams, void * bleParams);

//This type will act as a container for all 
//settings relating to the current sequencer project
typedef struct
{
    char fileName[MAX_FILENAME_CHARS + 1]; // plus one for termination
    uint8_t projectTempo;
    uint8_t quantization;
} ProjectParameters_t;



//Statically allocate all memory and objects 
//required for the gui menu RTOS task
static TaskHandle_t g_GUIMenuTaskHandle;
static StaticTask_t g_GUIMenuTaskBuffer;
static StackType_t g_GUIMenuTaskStack[GUI_MENU_TASK_STACK_SIZE];


//Statically allocate all memory and objects 
//required for the BLE gatt client RTOS task
static TaskHandle_t g_BleClientTaskHandle;
static StaticTask_t g_BleClientTaskBuffer;
static StackType_t g_BleClientTaskStack[BLE_CLIENT_TASK_STACK_SIZE];


//Statically allocate all memory and objects 
//required for the switch matrix RTOS task
static TaskHandle_t g_SwitchMatrixTaskHandle;    
static StaticTask_t g_SwitchMatrixTaskBuffer; //Private high-level RTOS task data
static StackType_t g_SwitchMatrixTaskStack[MATRIX_SCANNER_TASK_STACK_SIZE];


//This pointer is allocated memory from PSRAM at system startup. 
//The allocated memory is used as the systems midi file buffer,
//holding the midi file data relating to the current project.
uint8_t * g_midiFileBufferPtr = NULL;




//---- Public
void system_EntryPoint(void)
{
    uint8_t menuInputOpcode;

    bool isGridActive = false;
    bool hasEncoderInput = false;
    bool hasGridInput = false;

    //Allocate midi file buffer from PSRAM
    g_midiFileBufferPtr = heap_caps_malloc(FILE_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    assert(g_midiFileBufferPtr != NULL);
    //Keep a read-only record of the file cache base address
    const uint8_t * const midiFileBufferBASEPtr = g_midiFileBufferPtr;

    //Initialize and mount the file system
    FileSysPublicData_t FileSysInfo = fileSys_init();
    assert(*FileSysInfo.isPartitionMountedPtr == true);
    
    //Initialize sub-modules
    IPSDisplay_init();
    rotaryEncoders_init();

    //Update system task priority
    //vTaskPrioritySet(NULL, 3);

    //Now we want to initialize the RTOS tasks and assosiated
    //queues that make up the various system runtime processes.
    initRTOSTasks(&FileSysInfo, NULL, NULL);

    ESP_LOGI(LOG_TAG, "ENTERING MAIN SYSTEM LOOP");

    while (1)
    {
        if(xQueueReceive(g_MenuToSystemQueueHandle, &menuInputOpcode, portMAX_DELAY) == pdTRUE)
        {
            switch(menuInputOpcode)
            {
                case 1:
                    break;

                case 2:
                    break;

                case 3:
                    break;

                default:
                    assert(0);
                    break;
            }
        }
    }

    assert(0);
}




static void initRTOSTasks(void * menuParams, void * switchMatrixParams, void * bleParams)
{
    //--------------------------------------------------
    //---------------- MENU INTERFACE ------------------
    //--------------------------------------------------
    g_MenuToSystemQueueHandle = xQueueCreate(1, sizeof(uint8_t));
    assert(g_MenuToSystemQueueHandle != NULL);

    g_GUIMenuTaskHandle = xTaskCreateStaticPinnedToCore(guiMenu_entryPoint, "guiMenu", GUI_MENU_TASK_STACK_SIZE,
                                                        menuParams, 2, g_GUIMenuTaskStack, &g_GUIMenuTaskBuffer, 0);

    //--------------------------------------------------
    //------------- SWITCH MATRIX TASK -----------------
    //--------------------------------------------------
    g_SwitchMatrixQueueHandle = xQueueCreate(SWITCH_MATRIX_QUEUE_NUM_ITEMS, sizeof(SwitchMatrixQueueItem_t));
    assert(g_SwitchMatrixQueueHandle != NULL);

    g_SwitchMatrixTaskHandle = xTaskCreateStaticPinnedToCore(switchMatrix_TaskEntryPoint, "switchMatrixTask", MATRIX_SCANNER_TASK_STACK_SIZE,
                                                            switchMatrixParams, 1, g_SwitchMatrixTaskStack, &g_SwitchMatrixTaskBuffer, 0);

    vTaskSuspend(g_SwitchMatrixTaskHandle);

    //--------------------------------------------------
    //-------------- BLUETOOTH TASK --------------------
    //--------------------------------------------------
    g_HostToBleQueueHandle = xQueueCreate(10, sizeof(HostToBleQueueItem_t));
    g_BleToHostQueueHandle = xQueueCreate(10, sizeof(uint8_t));
    assert(g_HostToBleQueueHandle != NULL); 
    assert(g_BleToHostQueueHandle != NULL);

    g_BleClientTaskHandle = xTaskCreateStaticPinnedToCore(bleCentAPI_task, "bleClientTask", BLE_CLIENT_TASK_STACK_SIZE,
                                                        bleParams, 1, g_BleClientTaskStack, &g_BleClientTaskBuffer, 1);
    vTaskSuspend(g_BleClientTaskHandle);
}