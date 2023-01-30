#define INCLUDE_vTaskSuspend 1
#define configSUPPORT_DYNAMIC_ALLOCATION 1

#include <stdio.h>
#include <driver/gpio.h>
#include "system.h"


#define LOG_TAG "main"
#define LED0_IO 16
#define LED1_IO 17
#define LED_IO_CONFIG_MASK ((1ULL << LED1_IO) | (1ULL << LED0_IO))
#define ESP_INTR_FLAG_LEVEL1 1

static void setSystemLED(uint8_t led, bool isOnOff);

void app_main(void)
{
    gpio_config_t systemAliveLED;

    systemAliveLED.intr_type = GPIO_INTR_DISABLE;
    systemAliveLED.mode = GPIO_MODE_OUTPUT;
    systemAliveLED.pin_bit_mask = LED_IO_CONFIG_MASK;
    systemAliveLED.pull_down_en = false;
    systemAliveLED.pull_up_en = false;
    assert(gpio_config(&systemAliveLED) == ESP_OK);

    //setSystemLED(LED0_IO, true);

    //Enable per-pin intrrupt functionality, all gpio interrupts
    //share the same priority level. The only interrupts used on
    //this core are gpio driven so default priority level is fine
    assert(gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1) == ESP_OK);

    while (1)
    {
        system_EntryPoint();
        assert(0);
    }
}


static void setSystemLED(uint8_t led, bool isOnOff)
{
    if (led == LED0_IO || led == LED1_IO)
    {
        if (isOnOff)
            gpio_set_level(led, 1);
        else
            gpio_set_level(led, 0);
    }
}