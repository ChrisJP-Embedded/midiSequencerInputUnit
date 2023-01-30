#include "esp_log.h"
#include "include/ipsDisplay.h"
#include "systemTextFont/include/Font16.h"
#include "systemTextFont/include/Font32.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"


#define LOG_TAG "IPSDisplayDriver"

#define SCREEN_BACKLIGHT_SW_IO      46
#define SCREEN_SPI_SCK_IO           12
#define SCREEN_SPI_MOSI_IO          13
#define SCREEN_SPI_CS_IO            10
#define SCREEN_DATA_CMD_IO          11
#define SCREEN_nRESET_LINE_IO       9
#define SCREEN_CMD                  0
#define SCREEN_DATA                 1
#define NUM_BITS_IN_BYTE            8

#define SCREEN_IO_CONFIG_MASK ((1ULL << SCREEN_DATA_CMD_IO) | (1ULL << SCREEN_nRESET_LINE_IO) | (1ULL << SCREEN_BACKLIGHT_SW_IO))


const char characterSet[CHARACTERSET_MAX_IDX] = {
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'
};


typedef struct
{
    eFontSize_t fontSize;
    const unsigned char *fontWidthTablePtr;
    const unsigned char * const * fontCharPixelMap;
    uint8_t charHeightInPixels;
} sScreenSettings_t;


static sScreenSettings_t screenSettings = 
{
    .fontCharPixelMap = chrtbl_f32,
    .fontWidthTablePtr = widtbl_f32,
    .charHeightInPixels = chr_hgt_f32,
    .fontSize = fs32
};


typedef struct
{
    uint16_t xStart;
    uint16_t xEnd;
    uint16_t yStart;
    uint16_t yEnd;
    uint32_t numPixels;
} sScreenPositionData_t;


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


typedef struct
{
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; // No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} sScreenInitSequence_cmd_t;


//Each element of this array is an
static sScreenInitSequence_cmd_t st_init_cmds[] = {

    /* Memory Data Access Control, MX=MV=1, MY=ML=MH=0, RGB=0 */
    {MADCTL_REG_ADDR, {(1 << 5)|(1 << 6)}, 1},       
    /* Interface Pixel Format, 16bits/pixel for RGB/MCU interface */
    {COLMOD_REG_ADDR, {0x55}, 1},
    /* Porch Setting */
    {PORCTL_REG_ADDR, {0x0c, 0x0c, 0x00, 0x33, 0x33}, 5},
    /* Gate Control, Vgh=13.65V, Vgl=-10.43V */
    {GCTRL_REG_ADDR, {0x45}, 1},
    /* VCOM Setting, VCOM=1.175V */
    {VCOMS_REG_ADDR, {0x2B}, 1},
    /* LCM Control, XOR: BGR, MX, MH */
    {LCMCTRL_REG_ADDR, {0x2C}, 1},
    /* VDV and VRH Command Enable, enable=1 */
    {VDVVRHEN_REG_ADDR, {0x01, 0xff}, 2},
    /* VRH Set, Vap=4.4+... */
    {VRHS_REG_ADDR, {0x11}, 1},
    /* VDV Set, VDV=0 */
    {VDVS_REG_ADDR, {0x20}, 1},
    /* Frame Rate Control, 60Hz, inversion=0 */
    {FRCTRL2_REG_ADDR, {0x0f}, 1},
    /* Power Control 1, AVDD=6.8V, AVCL=-4.8V, VDDS=2.3V */
    {PWCTRL1_REG_ADDR, {0xA4, 0xA1}, 1},
    /* Positive Voltage Gamma Control */
    {PVGAMCTRL_REG_ADDR, {0xD0, 0x00, 0x05, 0x0E, 0x15, 0x0D, 0x37, 0x43, 0x47, 0x09, 0x15, 0x12, 0x16, 0x19}, 14},
    /* Negative Voltage Gamma Control */
    {NVGAMCTRL_REG_ADDR, {0xD0, 0x00, 0x05, 0x0D, 0x0C, 0x06, 0x2D, 0x44, 0x40, 0x0E, 0x1C, 0x18, 0x16, 0x19}, 14},
    /* Sleep Out */
    {SLPOUT_REG_ADDR, {0}, 0x80},
    /* Display On */
    {DISPON_REG_ADDR, {0}, 0x80},
    {0, {0}, 0xff}
};

DMA_ATTR static uint16_t pixelData16bit[2400] = {0xff}; // Make a virtual bit matrix representing pixels on screen

static spi_device_handle_t screenSPI;
static spi_device_interface_config_t UIScreenSPI_interface_conf = {};
static spi_bus_config_t UIScreenSPI_conf = {};
static gpio_config_t UIScreenPins_conf = {};

static void screenPreTransferCallback(spi_transaction_t *t);
static void drawToScreen(sScreenPositionData_t *screenPosData, sScreenColour_t colour);
static void waitForScreenBusy(void);
static void screenSendCMD(const uint8_t cmd);
static void screenSendData(const uint8_t *data, uint8_t dataLengthInBytes);


//
IRAM_ATTR void screenPreTransferCallback(spi_transaction_t *t)
{
    gpio_set_level(SCREEN_DATA_CMD_IO, (uint32_t)t->user);
}





//********************************************
//************ EXPOSED FUNCTIONS *************
//********************************************

void initIPSDisplayDriver(void)
{
    uint16_t cmd = 0;

    //*************************************
    //****** Configure MCU hardware *******
    //*************************************

    UIScreenPins_conf.intr_type = GPIO_INTR_DISABLE;
    UIScreenPins_conf.mode = GPIO_MODE_OUTPUT;
    UIScreenPins_conf.pin_bit_mask = SCREEN_IO_CONFIG_MASK;
    UIScreenPins_conf.pull_down_en = false;
    UIScreenPins_conf.pull_up_en = false;
    gpio_config(&UIScreenPins_conf);


    UIScreenSPI_conf.miso_io_num = -1;
    UIScreenSPI_conf.mosi_io_num = SCREEN_SPI_MOSI_IO;
    UIScreenSPI_conf.sclk_io_num = SCREEN_SPI_SCK_IO;
    UIScreenSPI_conf.quadwp_io_num = -1;
    UIScreenSPI_conf.quadhd_io_num = -1;
    UIScreenSPI_conf.max_transfer_sz = 4096; // MAY NEED TO CHANGE THIS LATER!
    spi_bus_initialize(SCREEN_USING_SPI_INSTANCE, &UIScreenSPI_conf, SPI_DMA_CH_AUTO);


    UIScreenSPI_interface_conf.clock_speed_hz = 10 * 1000 * 1000;
    UIScreenSPI_interface_conf.mode = 0;
    UIScreenSPI_interface_conf.spics_io_num = SCREEN_SPI_CS_IO;
    UIScreenSPI_interface_conf.queue_size = 7;
    UIScreenSPI_interface_conf.pre_cb = screenPreTransferCallback;
    spi_bus_add_device(SCREEN_USING_SPI_INSTANCE, &UIScreenSPI_interface_conf, &screenSPI);


    //*************************************
    //******** Initialize Display *********
    //*************************************


    gpio_set_level(SCREEN_nRESET_LINE_IO, false);
    vTaskDelay(10);
    gpio_set_level(SCREEN_nRESET_LINE_IO, true);
    vTaskDelay(10);

    // Send all the commands
    while (st_init_cmds[cmd].databytes != 0xff)
    {
        screenSendCMD(st_init_cmds[cmd].cmd);
        screenSendData(st_init_cmds[cmd].data, st_init_cmds[cmd].databytes & 0x1F);
        if (st_init_cmds[cmd].databytes & 0x80)
        {
            vTaskDelay(100);
        }
        cmd++;
    }
    
    vTaskDelay(50);
    fillScreenWithColour(screenColourBlack);
    vTaskDelay(50);
    gpio_set_level(SCREEN_BACKLIGHT_SW_IO, true);
}





uint16_t drawLineOfTextToScreen(char *textData, uint16_t len, uint16_t xStart, uint16_t yStart, sScreenColour_t colour)
{
    uint8_t b;
    uint32_t runLength;
    bool isSetClear;
    esp_err_t ret;
    uint32_t totalNumPixels;
    uint16_t pixelDataIdx = 0;
    uint32_t widthInPixels;
    uint16_t totalStringLengthInPixels= 0 ;
    uint16_t charTableIdx;

    static spi_transaction_t trans[6];

    //Prepare transmission config
    for(uint8_t x = 0; x < 6; x++)
    {
        memset(&trans[x], 0, sizeof(spi_transaction_t));
        if ((x & 1) == 0)
        {
            // Even transfers are commands
            trans[x].length = 8;
            trans[x].user = (void *)0;
        }
        else
        {
            // Odd transfers are data
            trans[x].length = 8 * 4;
            trans[x].user = (void *)1;
        }
        trans[x].flags = SPI_TRANS_USE_TXDATA;
    }

    for (uint16_t a = 0; a < len; ++a) // Iterate through string
    {

        if (xStart > SCREEN_NUM_X_PIXELS)
            break;


        charTableIdx = (uint16_t)((uint8_t)textData[a] - 32); // Adjust ASCII code to use as array index
        widthInPixels = widtbl_f32[charTableIdx];             // screenSettings.fontWidthTablePtr[charTableIdx];       //Find character width in num pixels
        totalNumPixels = widthInPixels * chr_hgt_f32;

        //printf("Loop iteration for character: %c\n", textData[a]);
        //printf("charTableIdx: %d\n", charTableIdx);
        //printf("Character width in pixels: %d\n", widthInPixels);

        pixelDataIdx = 0;

        if (screenSettings.fontSize == fs16)
        {
            // populate pixel data buffer
            for (b = 0; b < chr_hgt_f16; ++b)
            {
                for (uint8_t c = 0; c < widthInPixels; ++c)
                {
                    if (chrtbl_f32[charTableIdx][b] & (0x80 >> c))
                    {
                        pixelData16bit[pixelDataIdx] = colour;
                        pixelDataIdx++;
                    }
                    else
                    {
                        pixelData16bit[pixelDataIdx] = screenColourBlack;
                        pixelDataIdx++;
                    }
                }
            }
        }
        else
        {
            b = 0;
        loop:
            isSetClear = false;
            if (chrtbl_f32[charTableIdx][b] & 0x80) isSetClear = true; // Set binary bits

            runLength = (uint32_t)(chrtbl_f32[charTableIdx][b] & 0x7F) + 1;

            //printf("Current Byte: %0x\n", (uint8_t)chrtbl_f32[charTableIdx][b]);
            //printf("Run Length: %d\n", runLength);
            //printf("pixelDataIdx: %d\n", pixelDataIdx);

            for (uint32_t c = 0; c < runLength; ++c)
            {
                if (isSetClear == true)
                {
                    pixelData16bit[pixelDataIdx] = colour;
                    pixelDataIdx++;
                }
                else
                {
                    pixelData16bit[pixelDataIdx] = screenColourBlack;
                    pixelDataIdx++;
                }
            }
            if (pixelDataIdx < totalNumPixels)
            {
                ++b;
                goto loop;
            }
        }

        //printf("SPI transaction length in bits: %d\n", (pixelDataIdx * 16));

        trans[0].tx_data[0] = 0x2A;                                               //*** Send CASET CMD (Coloum Address Set - defining RAM write area) ***
        trans[1].tx_data[0] = (uint8_t)(xStart >> 8);                             // Col START addr MSB
        trans[1].tx_data[1] = (uint8_t)(xStart & 0x00ff);                         // Col START addr LSB
        trans[1].tx_data[2] = (uint8_t)((xStart + (widthInPixels - 1)) >> 8);     // Col END addr MSB
        trans[1].tx_data[3] = (uint8_t)((xStart + (widthInPixels - 1)) & 0x00ff); // Col END addr LSB

        trans[2].tx_data[0] = 0x2B;                                       //*** Send RASET CMD (Row Address Set - defining RAM write area) ***
        trans[3].tx_data[0] = (uint8_t)(yStart >> 8);                     // Row START addr MSB
        trans[3].tx_data[1] = (uint8_t)(yStart & 0x00FF);                 // Row START addr LSB
        trans[3].tx_data[2] = (uint8_t)((yStart + chr_hgt_f32) > 8);      // Row END addr MSB
        trans[3].tx_data[3] = (uint8_t)((yStart + chr_hgt_f32) & 0x00FF); // Row END addr LSB

        trans[4].tx_data[0] = 0x2C;           //*** Send RAMWR CMD to ST7789V2 (Memory write) ***
        trans[5].tx_buffer = &pixelData16bit; // Pass pointer to the data for write
        trans[5].length = pixelDataIdx * 16;  // Specify data length (in bits)
        trans[5].flags = 0;
        trans[5].rxlength = 0;

        // Queue SPI transactions
        for (uint8_t x = 0; x < 6; x++)
        {
            ret = spi_device_queue_trans(screenSPI, &trans[x], portMAX_DELAY);
            assert(ret == ESP_OK);
        }

        waitForScreenBusy();

        xStart += widthInPixels + 1;
        totalStringLengthInPixels += widthInPixels + 1;
    }

    //printf("exit method\n");
    return totalStringLengthInPixels;
}


void drawRectangleToScreen(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd, sScreenLineThickness_t linePx, sScreenColour_t colour)
{
    // Line thickness is pixels is always added in the positive x or y direction, with reference to screen
    // therefore when drawing objects using lines the thickness in px must be adjusted for if greater than 1px

    drawVerticalLineToScreen(yStart, yEnd, xStart, linePx, colour);
    drawVerticalLineToScreen(yStart, yEnd, (xEnd - (uint8_t)(linePx - 1)), linePx, colour);
    drawHorizontalLineToScreen(xStart, xEnd, yStart, linePx, colour);
    drawHorizontalLineToScreen(xStart, xEnd, (yEnd - (uint8_t)(linePx - 1)), linePx, colour);
}


void fillScreenWithColour(sScreenColour_t colour)
{
    sScreenPositionData_t screenDat;

    // The display is mounted with a roatation of 90 ccw, therefore
    // what we see as rows (x) and colums (y) are actually reversed

    screenDat.xStart = 0;
    screenDat.xEnd = SCREEN_NUM_Y_PIXELS;
    screenDat.yStart = 0;
    screenDat.yEnd = SCREEN_NUM_X_PIXELS;
    drawToScreen(&screenDat, colour);
}


void drawHorizontalLineToScreen(uint16_t xStart, uint16_t xEnd, uint16_t yPos, sScreenLineThickness_t lineThicknessPx, sScreenColour_t colour)
{
    sScreenPositionData_t screenDat;

    //The display is mounted with a roatation of 90 ccw, therefore
    //what we see as rows (x) and colums (y) are actually reversed
    screenDat.xStart = yPos;
    screenDat.xEnd = yPos + (lineThicknessPx - 1);
    screenDat.yStart = xStart;
    screenDat.yEnd = xEnd;
    drawToScreen(&screenDat, colour);
}


void drawVerticalLineToScreen(uint16_t yStart, uint16_t yEnd, uint16_t xPos, sScreenLineThickness_t lineThicknessPx, sScreenColour_t colour)
{
    sScreenPositionData_t screenDat;

    //The display is mounted with a roatation of 90 ccw, therefore
    //what we see as rows (x) and colums (y) are actually reversed
    screenDat.xStart = yStart;
    screenDat.xEnd = yEnd;
    screenDat.yStart = xPos;
    screenDat.yEnd = xPos + (lineThicknessPx - 1);
    drawToScreen(&screenDat, colour);
}


uint8_t getCharWidthInPixels(char c)
{
    uint16_t charTableIdx = (uint16_t)((uint8_t)c - 32); // Adjust ASCII code to use as array index
    return screenSettings.fontWidthTablePtr[charTableIdx];
}

uint8_t getCharHeightInPixels(void)
{
    if (screenSettings.fontSize == fs16) return (uint8_t)chr_hgt_f16;
    else if(screenSettings.fontSize == fs32) return (uint8_t)chr_hgt_f32;

    return 0;   //shouldn't ever reach here!
}


//********************************************
//************ PRIVATE FUNCTIONS *************
//********************************************

static void drawToScreen(sScreenPositionData_t *screenPosData, sScreenColour_t colour)
{
    //Unfortunately the current display module has the hardware configuration
    //of the ST7789V2 set to select a 4 wire interface - which means that the
    //data/command bit is controlled by its D/C pin - that pin has to be manually
    //SET to indicate DATA, and CLEARED to indicate COMMAND. 

    //This can be acheived by controlling the pin from an SPI peripheral callback which is 
    //automatically called at immediately before the start of an SPI transaction,
    //this adds to code complexity since we have to break down commands and data
    //into individual spi transactions.

    //Abort if receiving garbage arguments!
    if ((screenPosData->yEnd < screenPosData->yStart) || (screenPosData->xEnd < screenPosData->xStart)) return;
    else if (screenPosData->xEnd > SCREEN_NUM_Y_PIXELS || screenPosData->xStart > SCREEN_NUM_Y_PIXELS) return;
    else if (screenPosData->yEnd > SCREEN_NUM_X_PIXELS || screenPosData->yStart > SCREEN_NUM_X_PIXELS) return;

    uint16_t yPos;
    uint32_t numBitsInSPITransaction = 0;
    static spi_transaction_t trans[6] = {0};  //This is static as it will be used by non-blocking code

    for (uint8_t idx = 0; idx < 6; idx++)
    {
        if ((idx & 0x01) == 0)
        {
            // Even transfers are commands
            trans[idx].length = 8;
            trans[idx].user = (void *)0;
        }
        else
        {
            // Odd transfers are data
            trans[idx].length = 8 * 4;
            trans[idx].user = (void *)1;
        }
        trans[idx].flags = SPI_TRANS_USE_TXDATA;
    }

    for (uint16_t idx = 0; idx < ((screenPosData->xEnd - screenPosData->xStart) + 1); idx++)
    {
        pixelData16bit[idx] = colour;
        numBitsInSPITransaction++;
    }


    yPos = screenPosData->yStart;
    while (yPos <= screenPosData->yEnd) //For each line to draw
    {

        trans[0].tx_data[0] = 0x2A;                         //*** Send CASET CMD (Coloum Address Set - defining RAM write area) ***
        trans[1].tx_data[0] = (uint8_t)(yPos >> 8);         // Col START addr MSB
        trans[1].tx_data[1] = (uint8_t)(yPos & 0x00ff);     // Col START addr LSB
        trans[1].tx_data[2] = (uint8_t)(yPos >> 8);         // Col END addr MSB
        trans[1].tx_data[3] = (uint8_t)(yPos & 0x00ff);     // Col END addr LSB

        trans[2].tx_data[0] = 0x2B;                                         //*** Send RASET CMD (Row Address Set - defining RAM write area) ***
        trans[3].tx_data[0] = (uint8_t)(screenPosData->xStart >> 8);        // Row START addr MSB
        trans[3].tx_data[1] = (uint8_t)(screenPosData->xStart & 0x00FF);    // Row START addr LSB
        trans[3].tx_data[2] = (uint8_t)(screenPosData->xEnd >> 8);          // Row END addr MSB
        trans[3].tx_data[3] = (uint8_t)(screenPosData->xEnd & 0x00FF);      // Row END addr LSB

        trans[4].tx_data[0] = 0x2C;                     //*** Send RAMWR CMD to ST7789V2 (Memory write) ***

        trans[5].tx_buffer = &pixelData16bit;           // Pass pointer to the data for write
        trans[5].length = numBitsInSPITransaction*16;      // Specify data length (in bits)
        trans[5].flags = 0;                             //*** Take screen out of RAM write state by sending NOP CMD ***
        trans[5].rxlength = 0;

        // Queue SPI transactions
        for (uint8_t idx = 0; idx < 6; idx++)
        {
            spi_device_queue_trans(screenSPI, &trans[idx], portMAX_DELAY);
        }

        // SOME KIND OF TASK YEILD WILL PROBABLY WANT TO GO HERE!!
        waitForScreenBusy();
        yPos++;
    }
}


static void waitForScreenBusy(void)
{
    spi_transaction_t *rtrans;
    esp_err_t ret;

    // Wait for all 6 transactions to be done and get back the results.
    for (int x = 0; x < 6; x++)
    {
        //The IDF provides a way to check that a previously queued spi
        //transaction has completed - try to replace this with callbacks later
        ret = spi_device_get_trans_result(screenSPI, &rtrans, portMAX_DELAY);
        assert(ret == ESP_OK);
    }
}


static void screenSendCMD(const uint8_t cmd)
{
    //This function is ONLY used on startup while 
    //performing initial configuration of the ST7789V2.
    //DMA is used after conifguration complete
    spi_transaction_t t = {0};

    //The idf spi peripheral interface requires the 
    //data length to specified in number of BITS
    //Command length is just a bits so use macro
    t.length = NUM_BITS_IN_BYTE;

    t.tx_buffer = &cmd;  //The data is the cmd itself
    t.user = (void*)0;   //We must set the Data/Command line of the display via callback fulfill timing

    spi_device_polling_transmit(screenSPI, &t);
}


static void screenSendData(const uint8_t *data, uint8_t dataLengthInBytes)
{
    //This function is ONLY used on startup while 
    //performing initial configuration of the ST7789V2.
    //DMA is used after conifguration complete
    spi_transaction_t t = {0};

    //Abort if arguments are out of bounds
    if(dataLengthInBytes == 0 || data == NULL) return;

    //The idf spi peripheral interface requires the 
    //data length to specified in number of BITS
    t.length = dataLengthInBytes * NUM_BITS_IN_BYTE;


    t.tx_buffer = data;  //Pass address of data buffer to SPI interface
    t.user = (void*)1;   //We must set the Data/Command line of the display via callback to fulfill timing

    spi_device_polling_transmit(screenSPI, &t);
}




