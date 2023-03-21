#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "ledDrivers.h"
#include "include/switchMatrix.h"
#include <driver/gpio.h>

#define LOG_TAG                         "switchMatrix"
#define KEY_MATRIX_NUM_ROWS             SYSTEM_NUM_ROWS
#define KEY_MATRIX_NUM_COLUMNS          SYSTEM_NUM_COLUMNS
#define KEY_MATRIX_ROW0_IO              42      //Input pin
#define KEY_MATRIX_ROW1_IO              41      //Input pin
#define KEY_MATRIX_ROW2_IO              40      //Input pin
#define KEY_MATRIX_ROW3_IO              39      //Input pin
//#define KEY_MATRIX_ROW4_IO              38      //Input pin //temp removed
#define KEY_MATRIX_ROW5_IO              37      //Input pin
#define KEY_MATRIX_SCAN_CLK_IO          5       //Clock output pin
#define KEY_MATRIX_COUNTER_RESET_IO     4       //Counter reset output pin
#define KEY_MATRIX_START_COLUMN         7       

//Bit mask used to configure pins simultaneously
#define KEY_MATRIX_ROW_IO_CONFIG_MASK (1ULL << KEY_MATRIX_ROW0_IO)
//|(1ULL << KEY_MATRIX_ROW1_IO)|(1ULL << KEY_MATRIX_ROW2_IO) 
//(1ULL << KEY_MATRIX_ROW3_IO)|(1ULL << KEY_MATRIX_ROW5_IO)) 


//The switch matrix runs as a standalone RTOS task.
//The sequencer has 96 switches, that make up the sequencer input grid.
//In order to gather input from all switches, while minimizing IO usage the following approach is used:

//The switches are arranged in a 6x8 (row x column) switch matrix (the system considers the top left switch as column 0, row 0).
//An MC14017B decade counter is clocked by the MCU, its outputs are used to energize each column of switches sequentially.
//MC14017B output Q8 is tied to the ICs reset pin, such that the counter automatically resets itself as required.
//As the MC14017B startup state is undefined the MCU manually resets the MC14017B at startup only, which
//guarantees that the counter will start from output Q0 (the system must always know which output is energized).

//Each row of the switch matrix feeds into an input on the MCU (via schmitt triggers), a logic HIGH state on one of the 
//switch matrix inputs will trigger an interrupt to fire - the coordinate of the switch being pressed is then derived
//by considering the column number currently energized at time of interrupt and the row input which generated the interrupt.
//Switch events are communicated to the host system via a queue, where the grid coordinate of the event is passed to the queue.

//---- IMPORTANT NOTE ----//
//Due to PCB routing counter outputs Q0 - Q7 are connected to columns C7 - C0  
//respectfully, so the columns are scanned through in reverse order (right to left).



//---- Private ----//
static void switchMatrixSetup(void);
static void switchMatrixKeyPress_ISR(void *param);


//This queue is shared with the host system as an extern and acts
//to communicate all switch events from the module to the host system
QueueHandle_t g_SwitchMatrixQueueHandle;

//These are used to signal a switch event
//and row number of that event to the modules task 
static volatile bool g_switchEventFlagISR;
static volatile uint8_t g_switchEventRowNumISR;



//---- Public
void switchMatrix_TaskEntryPoint(void * taskParams)
{
    int8_t currentColumn = KEY_MATRIX_START_COLUMN;
    SwitchMatrixQueueItem SwitchEventQueueItem = {0};

    switchMatrixSetup();

    while(1)
    {
        if(g_switchEventFlagISR == true)
        {   
            SwitchEventQueueItem.row = g_switchEventRowNumISR;
            g_switchEventFlagISR = false;
            SwitchEventQueueItem.column = (uint8_t)currentColumn;
            //Queue item is copied across to queue IDF implmented
            //queue storage area, item can be overwritten after 'xQueueSend'
            xQueueSend(g_SwitchMatrixQueueHandle, &SwitchEventQueueItem, 0);
        }

        //Increment the decade counter 
        //by toggling its clock input pin
        gpio_set_level(KEY_MATRIX_SCAN_CLK_IO, true);
        vTaskDelay(pdMS_TO_TICKS(30));  //---- Sets switch matrix scan rate and debounces switches ----//
        gpio_set_level(KEY_MATRIX_SCAN_CLK_IO, false);

        currentColumn--; //Cycle through columns sequentially, then wrap around
        if(currentColumn < 0) currentColumn = KEY_MATRIX_START_COLUMN;
    }
}



//---- Private
static void switchMatrixSetup(void)
{
    //These are used as ISR params
    //to determine row of event
    static const uint8_t row0 = 0;
    //static const uint8_t row1 = 1;
    //static const uint8_t row2 = 2;
    //static const uint8_t row3 = 3;
    //static const uint8_t row4 = 4;
    //static const uint8_t row5 = 5;

    gpio_config_t switchMatrixInputPins_conf = {0};
    gpio_config_t counterControlPins_conf = {0};

    esp_err_t err = ESP_OK;

    //Configure input pins for the switch matrix
    switchMatrixInputPins_conf.intr_type = GPIO_INTR_POSEDGE;
    switchMatrixInputPins_conf.mode = GPIO_MODE_INPUT;
    switchMatrixInputPins_conf.pin_bit_mask = KEY_MATRIX_ROW_IO_CONFIG_MASK;
    switchMatrixInputPins_conf.pull_down_en = false;
    switchMatrixInputPins_conf.pull_up_en = false;
    err |= gpio_config(&switchMatrixInputPins_conf);
    assert(err == ESP_OK);

    err |= gpio_isr_handler_add(KEY_MATRIX_ROW0_IO, switchMatrixKeyPress_ISR, &row0);
    //err |= gpio_isr_handler_add(KEY_MATRIX_ROW1_IO, switchMatrixKeyPress_ISR, &row1);
    //err |= gpio_isr_handler_add(KEY_MATRIX_ROW2_IO, switchMatrixKeyPress_ISR, &row2);
    //err |= gpio_isr_handler_add(KEY_MATRIX_ROW3_IO, switchMatrixKeyPress_ISR, &row3);
    //err |= gpio_isr_handler_add(KEY_MATRIX_ROW4_IO, switchMatrixKeyPress_ISR, &row4);
    //err |= gpio_isr_handler_add(KEY_MATRIX_ROW5_IO, switchMatrixKeyPress_ISR, &row5);
    assert(err == ESP_OK);

    //Configure output pins for counter RESET and CLOCK
    counterControlPins_conf.intr_type = GPIO_INTR_DISABLE;
    counterControlPins_conf.mode = GPIO_MODE_OUTPUT;
    counterControlPins_conf.pin_bit_mask = ((1ULL << KEY_MATRIX_COUNTER_RESET_IO) | (1ULL << KEY_MATRIX_SCAN_CLK_IO));
    counterControlPins_conf.pull_down_en = false;
    counterControlPins_conf.pull_up_en = false;
    err |= gpio_config(&counterControlPins_conf);
    assert(err == ESP_OK);

    //Force counter into known state but toggling
    //its reset pin - this ony happens once at startup
    err |= gpio_set_level(KEY_MATRIX_COUNTER_RESET_IO, true);
    vTaskDelay(pdMS_TO_TICKS(10));
    err |= gpio_set_level(KEY_MATRIX_COUNTER_RESET_IO, false);

    //We no longer need to reset pin as the decade
    //counter will reset itself as required
    err |= gpio_reset_pin(KEY_MATRIX_COUNTER_RESET_IO);
    assert(err == ESP_OK);
}


//----------------------------------------------------
//---- MODULE INTERRUPT ROUTINES BELOW THIS POINT ----
//----------------------------------------------------


static void IRAM_ATTR switchMatrixKeyPress_ISR(void *param)
{
    if(!g_switchEventFlagISR)
    {
        g_switchEventRowNumISR = *((uint8_t *)param);
        g_switchEventFlagISR = true;
    }
}