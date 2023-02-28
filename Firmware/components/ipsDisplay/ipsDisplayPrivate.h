


//REGISTER ADDRESSES FOR 
//ST7789V2 DISPLAY DRIVER

#define CASET_PAYLOAD_SIZE_IN_BITS 32
#define RASET_PAYLOAD_SIZE_IN_BITS 32

#define CASET_REG_ADDR      0x2A
#define RASET_REG_ADDR      0x2B
#define RAMWR_REG_ADDR      0x2C

#define MADCTL_REG_ADDR     0x36
#define COLMOD_REG_ADDR     0x3A
#define PORCTL_REG_ADDR     0xB2
#define GCTRL_REG_ADDR      0xB7
#define VCOMS_REG_ADDR      0xBB
#define LCMCTRL_REG_ADDR    0xC0
#define VDVVRHEN_REG_ADDR   0xC2
#define VRHS_REG_ADDR       0xC3
#define VDVS_REG_ADDR       0xC4
#define FRCTRL2_REG_ADDR    0xC6
#define PWCTRL1_REG_ADDR    0xD0
#define PVGAMCTRL_REG_ADDR  0xE0
#define NVGAMCTRL_REG_ADDR  0xE1
#define SLPOUT_REG_ADDR     0x11
#define DISPON_REG_ADDR     0x29

#define END_OF_INITIALIZATION 0

#define NUM_SPI_TRANS_FOR_DRAW_TO_SCREEN 6

#define SET_DC_PIN_LOW ((void*)0)
#define SET_DC_PIN_HIGH ((void*)1)
#define SPI_TRANS_USE_TX_BUFFER 0 

#define SPACE_BETWEEN_CHARS_IN_PIXELS 1


typedef struct
{
    uint8_t registerAddr;
    uint8_t writePayload[16];
    uint8_t numBytesInPayload;
} ScreenInitCommand_t;


typedef struct
{
    uint16_t xStart;
    uint16_t xEnd;
    uint16_t yStart;
    uint16_t yEnd;
} ScreenPositionData_t;