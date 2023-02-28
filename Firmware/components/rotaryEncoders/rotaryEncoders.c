#include "esp_log.h"
#include "esp_attr.h"
#include "include/rotaryEncoders.h"
#include <driver/gpio.h>
#include "driver/gptimer.h"

#define LOG_TAG                 "rotaryEncoderComponent"
#define ENCODER0_SW_IO          48      //input pin
#define ENCODER1_SW_IO          45      //input pin
#define ENCODER0_PHA0_IO        18      //input pin
#define ENCODER0_PHA1_IO        8       //input pin
#define ENCODER1_PHA0_IO        21      //input pin
#define ENCODER1_PHA1_IO        14      //input pin
#define ENCODER_COUNT_PER_REV   4       //Count per revolution
#define ENCODER_QUEUE_SIZE      10

//All possible encoder output states
#define ENCODING_00     0
#define ENCODING_01     1
#define ENCODING_10     2
#define ENCODING_11     3

//Bit masks used to configure pins simultanously
#define ENCODER_SW_IO_CONFIG_MASK   ((1ULL << ENCODER0_SW_IO) | (1ULL << ENCODER1_SW_IO))
#define ENCODER_PHA_IO_CONFIG_MASK  ((1ULL << ENCODER0_PHA0_IO) | (1ULL << ENCODER1_PHA0_IO) | (1ULL << ENCODER0_PHA1_IO) | (1ULL << ENCODER1_PHA1_IO))


//---- Private ----//
static void setupDebounceTimer(void);
static void setupEncoderPins(void);
static void encoderPositionChange_ISR(void * eventParam);
static void encoderSwitchEvent_ISR(void * eventParam);
static bool alarmISR(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *param);

//Each encoder requires an instance
//of this type to track is state
typedef struct
{
    uint8_t ccwCount;
    uint8_t cwCount;
} sEncoderCount_t;


//The sequencer has two rotary encoders, each of which has a built in momentary switch.
//The rortary encoder code is completely interrupt driven, no need for a dedicated RTOS task.

//The system refers to the encoder nearest the display as 'Encoder0' and the one below it as 'Encoder1'
//Encoder1 can be considered as the 'primary' encoder, used to navigate the menu system.
//Encoder0 is used when additional nested functionality is required.

//Throughout the code base the following enumerations are used to describe encoder events:
//encoder0_sw   ->  Encoder 0 switch has been pressed
//encoder0_cw   ->  Encoder 0 has been turned in the clockwise direction
//encoder0_ccw  ->  Encoder 0 has been turned in the counter-clockwise direction
//encoder1_sw   ->  Encoder 1 switch has been pressed
//encoder1_cw   ->  Encoder 1 has been turned in the clockwise direction
//encoder1_ccw  ->  Encoder 1 has been turned in the counter-clockwise direction


//This queue is shared with the host system as an extern and acts
//to communicate all encoder events from the module to the host system
QueueHandle_t g_EncodersQueueHandle;

//A timer is used to provide debouncing
//of the switches on each of the encoders
static gptimer_handle_t g_debounceTimerHandle = NULL;
static volatile bool g_isWaitingForDebounceTimer = false;


//---- Public
void rotaryEncoders_init(void)
{
    //Create queue that module will use to 
    //communicate all encoder events to the host system
    g_EncodersQueueHandle = xQueueCreate(ENCODER_QUEUE_SIZE, sizeof(uint8_t));
    assert(g_EncodersQueueHandle != NULL);

    //Attempt configuration of encoder GPIO pins
    setupEncoderPins();

    //Provide debounce for encoder switches
    setupDebounceTimer();
}


//---- Public
void rotaryEncoders_deinit(void)
{
    esp_err_t err = ESP_OK;

    err |= gpio_isr_handler_remove(ENCODER0_SW_IO);
    err |= gpio_isr_handler_remove(ENCODER1_SW_IO);
    err |= gpio_isr_handler_remove(ENCODER0_PHA0_IO);
    err |= gpio_isr_handler_remove(ENCODER0_PHA1_IO);
    err |= gpio_isr_handler_remove(ENCODER1_PHA0_IO);
    err |= gpio_isr_handler_remove(ENCODER1_PHA1_IO);
    assert(err == ESP_OK);

    err |= gpio_reset_pin(ENCODER0_SW_IO);
    err |= gpio_reset_pin(ENCODER1_SW_IO);
    err |= gpio_reset_pin(ENCODER0_PHA0_IO);
    err |= gpio_reset_pin(ENCODER0_PHA1_IO);
    err |= gpio_reset_pin(ENCODER1_PHA0_IO);
    err |= gpio_reset_pin(ENCODER1_PHA1_IO);
    assert(err == ESP_OK);
}


//---- Private
static void setupEncoderPins(void)
{
    static const uint8_t encoder0_eventParam = encoder0_event;
    static const uint8_t encoder1_eventParam = encoder1_event;

    gpio_config_t UIRotaryEncoderSWPins_conf = {};
    gpio_config_t UIRotaryEncoderPHAPins_conf = {};

    esp_err_t err = ESP_OK;

    UIRotaryEncoderSWPins_conf.intr_type = GPIO_INTR_POSEDGE;
    UIRotaryEncoderSWPins_conf.mode = GPIO_MODE_INPUT;
    UIRotaryEncoderSWPins_conf.pin_bit_mask = ENCODER_SW_IO_CONFIG_MASK;
    UIRotaryEncoderSWPins_conf.pull_down_en = false;
    UIRotaryEncoderSWPins_conf.pull_up_en = false;
    err |= gpio_config(&UIRotaryEncoderSWPins_conf);
    assert(err == ESP_OK);

    UIRotaryEncoderPHAPins_conf.intr_type = GPIO_INTR_ANYEDGE;
    UIRotaryEncoderPHAPins_conf.mode = GPIO_MODE_INPUT;
    UIRotaryEncoderPHAPins_conf.pin_bit_mask = ENCODER_PHA_IO_CONFIG_MASK;
    UIRotaryEncoderPHAPins_conf.pull_down_en = false;
    UIRotaryEncoderPHAPins_conf.pull_up_en = false;
    err |= gpio_config(&UIRotaryEncoderPHAPins_conf);
    assert(err == ESP_OK);
    
    err |= gpio_isr_handler_add(ENCODER0_SW_IO, encoderSwitchEvent_ISR, (void*)(&encoder0_eventParam));
    err |= gpio_isr_handler_add(ENCODER1_SW_IO, encoderSwitchEvent_ISR, (void*)(&encoder1_eventParam));
    err |= gpio_isr_handler_add(ENCODER0_PHA0_IO, encoderPositionChange_ISR, (void*)(&encoder0_eventParam));
    err |= gpio_isr_handler_add(ENCODER0_PHA1_IO, encoderPositionChange_ISR, (void*)(&encoder0_eventParam));
    err |= gpio_isr_handler_add(ENCODER1_PHA0_IO, encoderPositionChange_ISR, (void*)(&encoder1_eventParam));
    err |= gpio_isr_handler_add(ENCODER1_PHA1_IO, encoderPositionChange_ISR, (void*)(&encoder1_eventParam));
    assert(err == ESP_OK);
}


//---- Private
static void setupDebounceTimer(void)
{
    esp_err_t err = ESP_OK;

    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_APB,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz
    };

    gptimer_event_callbacks_t cbs = {
        .on_alarm = alarmISR,
    };

    err |= gptimer_new_timer(&timer_config, &g_debounceTimerHandle);
    err |= gptimer_register_event_callbacks(g_debounceTimerHandle, &cbs, NULL);
    err |= gptimer_enable(g_debounceTimerHandle);
    assert(err == ESP_OK);
}


//----------------------------------------------------
//---- MODULE INTERRUPT ROUTINES BELOW THIS POINT ----
//----------------------------------------------------


//---- Private
static void IRAM_ATTR encoderSwitchEvent_ISR(void * eventParam)
{
    //This interrupt is executed when the momentary
    //switch of either encoder has been pressed by the user

    if(!g_isWaitingForDebounceTimer)
    {

        uint8_t eventType;

        //Determine which encoders switch was pressed
        switch(*(uint8_t*)eventParam)
        {
            case encoder0_event:
                eventType = encoder0_sw;
                break;

            case encoder1_event:
                eventType = encoder1_sw;
                break;

            default:
                assert(0);
                break;
        }

        //Place the new switch event into the encoder event queue
        xQueueSendToBackFromISR(g_EncodersQueueHandle, &eventType, pdFALSE);

        //Handle debounce timer
        g_isWaitingForDebounceTimer = true;
        gptimer_alarm_config_t alarm_config = {
            .alarm_count = 200000, // 100ms @ 1Mhz
        };
        gptimer_set_alarm_action(g_debounceTimerHandle, &alarm_config);
        gptimer_start(g_debounceTimerHandle);
    }
}


//---- Private
static void IRAM_ATTR encoderPositionChange_ISR(void *eventParam)
{
    //This interrupt is executed when one of the encoders is rotated
    //might look like a lot of code but between static variables and
    //switches it isnt a lot of code that actually gets executed

    static sEncoderCount_t Encoder0Count;
    static uint8_t encoder0_previousState;

    static sEncoderCount_t Encoder1Count;
    static uint8_t encoder1_previousState;

    sEncoderCount_t * EncoderCountPtr = NULL;
    uint8_t * previousStatePtr = NULL;

    uint8_t eventType;
    uint8_t currentPhaseEncoding;

    //Determine which encoders switch was pressed
    //and get direct pointer to relevant data
    switch(*(uint8_t*)eventParam)
    {
        case encoder0_event:
            EncoderCountPtr = &Encoder0Count;
            previousStatePtr = &encoder0_previousState;
            //Get the current state of encoder0 inputs
            currentPhaseEncoding = ((gpio_get_level(ENCODER0_PHA1_IO) << 1) | gpio_get_level(ENCODER0_PHA0_IO));
            break;

        case encoder1_event:
            EncoderCountPtr = &Encoder1Count;
            previousStatePtr = &encoder1_previousState;
            //Get the current state of encoder1 inputs
            currentPhaseEncoding = ((gpio_get_level(ENCODER1_PHA1_IO) << 1) | gpio_get_level(ENCODER1_PHA0_IO));
            break;

        default:
            assert(0);
            break;
    }

    //Encoder state machine handling
    switch (currentPhaseEncoding)
    {
        case ENCODING_00: //Both encoder inputs LOW
            if (*previousStatePtr == ENCODING_10)
            {
                EncoderCountPtr->cwCount++;
                EncoderCountPtr->ccwCount = 0;
            }
            else if (*previousStatePtr == ENCODING_01)
            {
                EncoderCountPtr->ccwCount++;
                EncoderCountPtr->cwCount = 0;
            }
            break;

        case ENCODING_01: //Encoder PHA0 HIGH only
            if (*previousStatePtr == ENCODING_00)
            {
                EncoderCountPtr->cwCount++;
                EncoderCountPtr->ccwCount = 0;
            }
            else if (*previousStatePtr == ENCODING_11)
            {
                EncoderCountPtr->ccwCount++;
                EncoderCountPtr->cwCount = 0;
            }
            break;

        case ENCODING_10: //Encoder PHA1 HIGH only
            if (*previousStatePtr == ENCODING_11)
            {
                EncoderCountPtr->cwCount++;
                EncoderCountPtr->ccwCount = 0;
            }
            else if (*previousStatePtr == ENCODING_00)
            {
                EncoderCountPtr->ccwCount++;//Clear count
                EncoderCountPtr->cwCount = 0;
            }
            break;

        case ENCODING_11: //Both encoder inputs HIGH
            if (*previousStatePtr == ENCODING_01)
            {
                EncoderCountPtr->cwCount++;
                EncoderCountPtr->ccwCount = 0;
            }
            else if (*previousStatePtr == ENCODING_10)
            {
                EncoderCountPtr->ccwCount++;
                EncoderCountPtr->cwCount = 0;
            }
            break;
    }

    //Keep record of previous state of encoder
    *previousStatePtr = currentPhaseEncoding;

    //Place event in queue if CPR threshold reached
    if(EncoderCountPtr->cwCount >= ENCODER_COUNT_PER_REV)    
    {
        //cw event detected,
        //determine which encoder
        switch(*(uint8_t*)eventParam)
        {
            case encoder0_event:
                eventType = encoder0_cw;
                break;

            case encoder1_event:
                eventType = encoder1_cw;
                break;

            default:
                assert(0);
                break;
        }
        //Inform system of the event
        xQueueSendToBackFromISR(g_EncodersQueueHandle, &eventType, pdFALSE);
        EncoderCountPtr->cwCount = 0; //Clear count
    }
    else if(EncoderCountPtr->ccwCount >= ENCODER_COUNT_PER_REV)   
    {
        //cw event detected,
        //determine which encoder
        switch(*(uint8_t*)eventParam)
        {
            case encoder0_event:
                eventType = encoder0_ccw;
                break;

            case encoder1_event:
                eventType = encoder1_ccw;
                break;

            default:
                assert(0);
                break;
        }
        //Inform system of the event
        xQueueSendToBackFromISR(g_EncodersQueueHandle, &eventType, pdFALSE);
        EncoderCountPtr->ccwCount = 0; //Clear count
    }
}


static bool alarmISR(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *param)
{
    gptimer_stop(g_debounceTimerHandle);
    gptimer_set_raw_count(g_debounceTimerHandle, 0);
    g_isWaitingForDebounceTimer = false;
    return false; //No task switch needed
}
