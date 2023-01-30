

#define NUM_RGB_LEDS 12
#define NUM_LEDS (NUM_RGB_LEDS * 3)

#define SYSTEM_NUM_ROWS 6
#define SYSTEM_NUM_COLUMNS 8

//*******************************************************
//The 'rgbLedColour_t' enumerations are used to store
//the values that will be loaded into the PWM registers for 
//any given RGB LED in order to produce the specified colour.
//We create a type so that we can easily create recognizable
//ledDriver data in other modules.
typedef enum {
    rgb_off    = 0x00000000,
    rgb_red    = 0x00FF0000,
    rgb_green  = 0x0000FF00,
    rgb_blue   = 0x000000FF,
    rgb_orange = 0x00FFA500,
    rgb_yellow = 0x00FFFF00,
    rgb_purple = 0x00CC33FF,
    rgb_cyan   = 0x0000FFFF,
    rgb_pink   = 0x00FFC0CB
} rgbLedColour_t;

//---- Module interface ----//
uint8_t ledDrivers_init(void);
uint8_t ledDrivers_writeSingleLed(uint8_t columnNum, uint8_t rowNum, rgbLedColour_t rgbColourCode);
uint8_t ledDrivers_writeSingleGridColumn(uint8_t columnNum, rgbLedColour_t * columnColoursPtr);
uint8_t ledDrivers_writeEntireGrid(rgbLedColour_t * rgbGridColours);
void ledDrivers_gridTestDemo(void);
void ledDrivers_blankOutEntireGrid(void);



