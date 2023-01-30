#include "stdio.h"
#include "include/ledDrivers.h"


#define NUM_8BIT_PWM_REGISTES_PER_LED 3
#define NUM_8BIT_PWM_REGISTERS_PER_COLUMN 18

#define CHIP_ENABLE_REG_ADDR    0x000
#define DEV_INITIAL_REG_ADDR    0x001
#define DEV_CONFIG1_REG_ADDR    0x002
#define DEV_CONFIG2_REG_ADDR    0x003
#define DEV_CONFIG4_REG_ADDR    0x004
#define GLOBAL_BRI_REG_ADDR     0x005


//----------------------------------------------
//The enumeration below contains the 10-bit register
//addresses for the LP5862 Driver IC PWM registers
//for register descriptions see LP5862 datasheet, p28-p29.

//These enumerations are loaded into the 'ledDriverPwmAddrRGB'
//array (declared below), in order to allow for a convenient 
//zero base loopup table.
enum
{
    // Listed in order of
    // location in memory
    ledGreen_L0_CS0  = 0x0200,
    ledRed_L0_CS1    = 0x0201,
    ledBlue_L0_CS2   = 0x0202,

    ledGreen_L0_CS3  = 0x0203,
    ledRed_L0_CS4    = 0x0204,
    ledBlue_L0_CS5   = 0x0205,

    ledGreen_L0_CS6  = 0x0206,
    ledRed_L0_CS7    = 0x0207,
    ledBlue_L0_CS8   = 0x0208,

    ledGreen_L0_CS9  = 0x0209,
    ledRed_L0_CS10   = 0x020A,
    ledBlue_L0_CS11  = 0x020B,

    ledGreen_L0_CS12 = 0x020C,
    ledRed_L0_CS13   = 0x020D,
    ledBlue_L0_CS14  = 0x020E,

    ledGreen_L0_CS15 = 0x020F,
    ledRed_L0_CS16   = 0x0210,
    ledBlue_L0_CS17  = 0x0211,

    ledGreen_L1_CS0  = 0x0212,
    ledRed_L1_CS1    = 0x0213,
    ledBlue_L1_CS2   = 0x0214,

    ledGreen_L1_CS3  = 0x0215,
    ledRed_L1_CS4    = 0x0216,
    ledBlue_L1_CS5   = 0x0217,

    ledGreen_L1_CS6  = 0x0218,
    ledRed_L1_CS7    = 0x0219,
    ledBlue_L1_CS8   = 0x021A,

    ledGreen_L1_CS9  = 0x021B,
    ledRed_L1_CS10   = 0x021C,
    ledBlue_L1_CS11  = 0x021D,

    ledGreen_L1_CS12 = 0x021E,
    ledRed_L1_CS13   = 0x021F,
    ledBlue_L1_CS14  = 0x0220,

    ledGreen_L1_CS15 = 0x0221,
    ledRed_L1_CS16   = 0x0222,
    ledBlue_L1_CS17  = 0x0223
};




//-----------------------------------------------
//The LP5862 ICs have a built-in feature which
//allows for drivers on the same I2C bus to be 
//addressed either individually OR all at once,
//This is descripted as independent and broadcast
//addressing within the LP5862 datasheet (p23)

//When addessing all drivers (I2C broadcast)
//use 'BROADCAST_CHIP_ADDRESS' as the chip address
#define BROADCAST_CHIP_ADDRESS 0x15

//When addressing a single device on the bus
//the I2C chip addresses are formed as shown below,
//where the upper nibble is a hardcoded value and
//lower nibble is determined by IC hardware config
#define INDEPENDENT_IC_ADDR_BITS 0x10

typedef enum
{
    ledDrvIC3 = (INDEPENDENT_IC_ADDR_BITS | 0x03),
    ledDrvIC2 = (INDEPENDENT_IC_ADDR_BITS | 0x02),
    ledDrvIC1 = (INDEPENDENT_IC_ADDR_BITS | 0x01),
    ledDrvIC0 = (INDEPENDENT_IC_ADDR_BITS | 0x00)
} ledDriverICAddr_t;
//-----------------------------------------------




//------------------------------------------------------------------
//Each driver IC drives 12 RGB LEDs, for any given RGB LED the
//driver has PWM registers for each of the individual red green
//and blue leds that make up that RGB LED component.

//The array below allows us to quickly look up those PWM registers
//from a conventient zero base index. 

//When looking at the sequencer
static const uint16_t ledDriverPwmAddrRGB[36] =
{
    ledGreen_L1_CS0,    
    ledRed_L1_CS1,
    ledBlue_L1_CS2,

    ledGreen_L1_CS15,
    ledRed_L1_CS16,
    ledBlue_L1_CS17, 

    ledGreen_L1_CS6,
    ledRed_L1_CS7,
    ledBlue_L1_CS8,

    ledGreen_L1_CS9,
    ledRed_L1_CS10,
    ledBlue_L1_CS11,

    ledGreen_L1_CS12,
    ledRed_L1_CS13,
    ledBlue_L1_CS14,

    ledGreen_L1_CS3,
    ledRed_L1_CS4,
    ledBlue_L1_CS5,

    ledGreen_L0_CS0,
    ledRed_L0_CS1,
    ledBlue_L0_CS2,

    ledGreen_L0_CS15,
    ledRed_L0_CS16,
    ledBlue_L0_CS17,

    ledGreen_L0_CS6,
    ledRed_L0_CS7,
    ledBlue_L0_CS8,
 
    ledGreen_L0_CS9,
    ledRed_L0_CS10,
    ledBlue_L0_CS11,

    ledGreen_L0_CS12,
    ledRed_L0_CS13,
    ledBlue_L0_CS14,

    ledGreen_L0_CS3,
    ledRed_L0_CS4,
    ledBlue_L0_CS5,
};