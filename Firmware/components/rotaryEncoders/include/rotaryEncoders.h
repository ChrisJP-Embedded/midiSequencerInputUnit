#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

enum {
    encoder0_event,
    encoder1_event,
    encoder0_sw,
    encoder0_cw,
    encoder0_ccw,
    encoder1_sw,
    encoder1_cw,
    encoder1_ccw
};

//Queue used to communicate all 
//events relating to the rotary encoders
extern QueueHandle_t g_EncodersQueueHandle;

//Module interface
void rotaryEncoders_init(void);
void rotaryEncoders_deinit(void);
