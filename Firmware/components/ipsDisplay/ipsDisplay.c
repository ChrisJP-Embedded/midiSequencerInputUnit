#include "esp_log.h"
#include "esp_attr.h"
#include "include/ipsDisplay.h"
#include "systemTextFont/include/Font16.h"
#include "systemTextFont/include/Font32.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "ipsDisplayPrivate.h"


#define LOG_TAG "IPSDisplay"
#define DISPLAY_BACKLIGHT_SW_IO      46
#define DISPLAY_SPI_SCK_IO           12
#define DISPLAY_SPI_MOSI_IO          13
#define DISPLAY_SPI_CS_IO            10
#define DISPLAY_DATA_CMD_IO          11
#define DISPLAY_nRESET_LINE_IO       9
#define NUM_BITS_IN_BYTE             8
#define PIXEL_BUFFER_SIZE            1024
#define MAX_CHARS_IN_STRING          20
#define DISPLAY_IO_CONFIG_MASK ((1ULL << DISPLAY_DATA_CMD_IO) | (1ULL << DISPLAY_nRESET_LINE_IO) | (1ULL << DISPLAY_BACKLIGHT_SW_IO))


static void spiDrawToScreenLowLevel(uint32_t numPixelsToDraw, ScreenPositionData_t * screenPosData);
static void drawToScreen(ScreenPositionData_t *screenPosData, ScreenColour colour);
static void displayPreTransferSPICallback(spi_transaction_t * param);
static inline void configureSPI(void);
static void waitForScreenBusy(void);


DMA_ATTR static uint16_t pixelBuffer16Bit[PIXEL_BUFFER_SIZE];
static spi_device_handle_t g_displaySPIHandle;

const char characterSet[CHARACTER_SET_NUM_CHARS] = {
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'
};



//---- Public
void IPSDisplay_init(void)
{
    uint16_t commandIdx = 0;
    spi_transaction_t spiTrans = {0};
    ScreenInitCommand_t displayDriverInitSequence[] = {

        //Memory Data Access Control, MX=MV=1, MY=ML=MH=0, RGB=0
        {MADCTL_REG_ADDR, {(1 << 5)|(1 << 6)}, 1},       
        //Interface Pixel Format, 16bits/pixel for RGB/MCU interface
        {COLMOD_REG_ADDR, {0x55}, 1},
        //Porch Setting
        {PORCTL_REG_ADDR, {0x0c, 0x0c, 0x00, 0x33, 0x33}, 5},
        //Gate Control, Vgh=13.65V, Vgl=-10.43V
        {GCTRL_REG_ADDR, {0x45}, 1},
        //VCOM Setting, VCOM=1.175V
        {VCOMS_REG_ADDR, {0x2B}, 1},
        //LCM Control, XOR: BGR, MX, MH
        {LCMCTRL_REG_ADDR, {0x2C}, 1},
        //VDV and VRH Command Enable, enable=1
        {VDVVRHEN_REG_ADDR, {0x01, 0xff}, 2},
        //VRH Set, Vap=4.4+...
        {VRHS_REG_ADDR, {0x11}, 1},
        //VDV Set, VDV=0
        {VDVS_REG_ADDR, {0x20}, 1},
        //Frame Rate Control, 60Hz, inversion=0
        {FRCTRL2_REG_ADDR, {0x0f}, 1},
        //Power Control 1, AVDD=6.8V, AVCL=-4.8V, VDDS=2.3V
        {PWCTRL1_REG_ADDR, {0xA4, 0xA1}, 1},
        //Positive Voltage Gamma Control
        {PVGAMCTRL_REG_ADDR, {0xD0, 0x00, 0x05, 0x0E, 0x15, 0x0D, 0x37, 0x43, 0x47, 0x09, 0x15, 0x12, 0x16, 0x19}, 14},
        //Negative Voltage Gamma Control
        {NVGAMCTRL_REG_ADDR, {0xD0, 0x00, 0x05, 0x0D, 0x0C, 0x06, 0x2D, 0x44, 0x40, 0x0E, 0x1C, 0x18, 0x16, 0x19}, 14},
        //Sleep Out
        {SLPOUT_REG_ADDR, {0}, 0},
        //Display On
        {DISPON_REG_ADDR, {0}, 0},
        //Signal end of init sequence
        {END_OF_INITIALIZATION, {0}, 0}
    };

    //------------------------------------
    //------ Configure MCU hardware ------
    //------------------------------------
    configureSPI();

    //------------------------------------
    //------- Initialize Display ---------
    //------------------------------------

    //First force display reset
    gpio_set_level(DISPLAY_nRESET_LINE_IO, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(DISPLAY_nRESET_LINE_IO, true);
    vTaskDelay(pdMS_TO_TICKS(10));

    //At system startup the display needs to be initialized to desired settings
    //This code iterates through an array of objects that hold initialization data
    while (displayDriverInitSequence[commandIdx].registerAddr != END_OF_INITIALIZATION)
    {
        //First set the register address as a command packet
        spiTrans.length = NUM_BITS_IN_BYTE;
        spiTrans.tx_data[0] = displayDriverInitSequence[commandIdx].registerAddr;
        spiTrans.flags = SPI_TRANS_USE_TXDATA;
        spiTrans.user = SET_DC_PIN_LOW;
        assert(spi_device_polling_transmit(g_displaySPIHandle, &spiTrans) == ESP_OK);

        //Now send payload to be written to specified register
        if(displayDriverInitSequence[commandIdx].numBytesInPayload > 0)
        {
            spiTrans.length = displayDriverInitSequence[commandIdx].numBytesInPayload * NUM_BITS_IN_BYTE;
            spiTrans.tx_buffer = displayDriverInitSequence[commandIdx].writePayload;
            spiTrans.flags = SPI_TRANS_USE_TX_BUFFER;
            spiTrans.user = SET_DC_PIN_HIGH;
            assert(spi_device_polling_transmit(g_displaySPIHandle, &spiTrans) == ESP_OK);
        }

        //These commands listed require a delay to be inserted
        switch(displayDriverInitSequence[commandIdx].registerAddr)
        {
            case SLPOUT_REG_ADDR:
            case DISPON_REG_ADDR:
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    
        ++commandIdx;
    }
    
    vTaskDelay(pdMS_TO_TICKS(50));
    IPSDisplay_fillScreenWithColour(screenColourBlack);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(DISPLAY_BACKLIGHT_SW_IO, true);
}


//---- Public
uint8_t IPSDisplay_getCharWidthInPixels(char charToCheck)
{
    return widtbl_f32[charToCheck - 32];
}


//---- Public
uint8_t IPSDisplay_getCharHeightInPixels(void)
{
    return chr_hgt_f32;
}


//---- Public
uint16_t IPSDisplay_drawLineOfTextToScreen(char *textData, uint16_t numCharsInString, uint16_t xStart, uint16_t yStart, ScreenColour colour)
{
    //This function handles drawing of a text string to the display,
    //the provided x,y coordinate defines the bottom left most pixel
    //at which the text string will be placed.

    assert(textData != NULL);
    assert(xStart < SCREEN_NUM_X_PIXELS);
    assert(yStart < SCREEN_NUM_Y_PIXELS);
    assert(numCharsInString < MAX_CHARS_IN_STRING);

    bool isPixelSet;
    uint8_t rlePixelMapIdx;
    uint8_t runLength;
    uint8_t widthInPixels;
    uint8_t charTableIdx;
    uint16_t totalPixelsForCurrentChar;
    uint16_t pixelDataBufferIdx;
    uint16_t totalStringLengthInPixels = 0;
    ScreenPositionData_t drawArea;

    //In this initial version we write to the display character by character,
    //in order to cut down the required buffer size for the pixel data.
    for (uint16_t currentChar = 0; currentChar < numCharsInString; ++currentChar)
    {
        //First step is determining the number of pixels
        //needed to draw the current character to the display.
        charTableIdx = textData[currentChar] - 32;
        widthInPixels = widtbl_f32[charTableIdx];
        totalPixelsForCurrentChar = widthInPixels * chr_hgt_f32;

        pixelDataBufferIdx = 0; 
        rlePixelMapIdx = 0;

        do
        {
            //NOTE: Run-Line-Encoded font.
            //Each font character has an assosiated byte array which 
            //provides a run-line-encoded pixel map for that character.
            //For each RLE encoded byte, the MSBIT acts flag and bits 6->0 
            //provide a value between 0-127. When MSBIT is SET the number
            //of pixels specified by bits 6->0 should be VISIBLE, meaning
            //they should be drawn in a foreground colour.
            //When the MSBIT is CLEAR the number of pixels specified by
            //bits 6->0 should not be visible (drawn in background colour).

            //Check if MSBIT of RLE byte is SET or CLEAR
            if (chrtbl_f32[charTableIdx][rlePixelMapIdx] & 0x80) isPixelSet = true;
            else isPixelSet = false;

            //Get the run length from bits 6->0 of RLE byte
            runLength = chrtbl_f32[charTableIdx][rlePixelMapIdx] & 0x7F;

            //For the number of pixels specified by the run length, 
            //either draw pixels to foreground (visible) or background.
            for(uint8_t pixelNum = 0; pixelNum <= runLength; ++pixelNum)
            {
                assert(pixelDataBufferIdx < PIXEL_BUFFER_SIZE);
                if (isPixelSet == true) pixelBuffer16Bit[pixelDataBufferIdx] = colour;
                else pixelBuffer16Bit[pixelDataBufferIdx] = screenColourBlack;
                ++pixelDataBufferIdx;
            }
                
            ++rlePixelMapIdx; //Increment idx onto next RLE byte

        }while(pixelDataBufferIdx < totalPixelsForCurrentChar);

        drawArea.xStart = xStart;
        drawArea.xEnd = (xStart + (widthInPixels - 1));
        drawArea.yStart = yStart;
        drawArea.yEnd = (yStart + (chr_hgt_f32 - 1));

        spiDrawToScreenLowLevel(totalPixelsForCurrentChar, &drawArea);

        //We update the xStart coordinate so the next character
        //written to display doesnt overlap the one just written
        xStart += widthInPixels + SPACE_BETWEEN_CHARS_IN_PIXELS;

        //Generate a record of the pixel width of the entire string being drawn
        totalStringLengthInPixels += widthInPixels + SPACE_BETWEEN_CHARS_IN_PIXELS;
    }

    return totalStringLengthInPixels;
}


//---- Public
void IPSDisplay_fillScreenWithColour(ScreenColour colour)
{
    ScreenPositionData_t screenDat;

    screenDat.yStart = 0;
    screenDat.yEnd = SCREEN_NUM_Y_PIXELS;
    screenDat.xStart = 0;
    screenDat.xEnd = SCREEN_NUM_X_PIXELS;
    drawToScreen(&screenDat, colour);
}


//---- Public
void IPSDisplay_drawRectangleToScreen(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd, LineThickness linePx, ScreenColour colour)
{
    assert(xEnd < SCREEN_NUM_X_PIXELS);
    assert(xStart < xEnd);
    assert(yEnd < SCREEN_NUM_Y_PIXELS);
    assert(yStart < yEnd);

    IPSDisplay_drawVerticalLineToScreen(yStart, yEnd, xStart, linePx, colour);
    IPSDisplay_drawVerticalLineToScreen(yStart, yEnd, (xEnd - (linePx - 1)), linePx, colour);
    IPSDisplay_drawHorizontalLineToScreen(xStart, xEnd, yStart, linePx, colour);
    IPSDisplay_drawHorizontalLineToScreen(xStart, xEnd, (yEnd - (linePx - 1)), linePx, colour);
}


//---- Public
void IPSDisplay_drawHorizontalLineToScreen(uint16_t xStart, uint16_t xEnd, uint16_t yPos, LineThickness lineThicknessPx, ScreenColour colour)
{
    assert(xEnd < SCREEN_NUM_X_PIXELS);
    assert(xStart < xEnd);
    assert(yPos < SCREEN_NUM_Y_PIXELS);

    ScreenPositionData_t screenDat;

    screenDat.yStart = yPos;
    screenDat.yEnd = yPos + (lineThicknessPx - 1);
    screenDat.xStart = xStart;
    screenDat.xEnd = xEnd;
    drawToScreen(&screenDat, colour);
}


//---- Public
void IPSDisplay_drawVerticalLineToScreen(uint16_t yStart, uint16_t yEnd, uint16_t xPos, LineThickness lineThicknessPx, ScreenColour colour)
{
    assert(yEnd < SCREEN_NUM_Y_PIXELS);
    assert(yStart < yEnd);
    assert(xPos < SCREEN_NUM_X_PIXELS);

    ScreenPositionData_t screenDat;

    screenDat.yStart = yStart;
    screenDat.yEnd = yEnd;
    screenDat.xStart = xPos;
    screenDat.xEnd = xPos + (lineThicknessPx - 1);
    drawToScreen(&screenDat, colour);
}


//---- Private
static void drawToScreen(ScreenPositionData_t *screenPosData, ScreenColour colour)
{
    //This function handles drawing of lines

    assert(screenPosData != NULL);

    uint16_t yPos;
    uint32_t numPixelsToDraw = 0;
    ScreenPositionData_t drawArea;

    //We have a finite pixel buffer so currently drawing one pixel row at a time
    for (uint16_t idx = 0; idx < (screenPosData->xEnd - screenPosData->xStart); idx++)
    {
        assert(numPixelsToDraw < PIXEL_BUFFER_SIZE);
        pixelBuffer16Bit[idx] = colour;
        numPixelsToDraw++;
    }

    yPos = screenPosData->yStart;
    while (yPos < screenPosData->yEnd)
    {
        drawArea.xStart = screenPosData->xStart;
        drawArea.xEnd = screenPosData->xEnd;
        drawArea.yStart = yPos;
        drawArea.yEnd = (yPos+1);
        
        spiDrawToScreenLowLevel(numPixelsToDraw, &drawArea);
        yPos++;
    }
}


//---- Private
static void spiDrawToScreenLowLevel(uint32_t numPixelsToDraw, ScreenPositionData_t * drawArea)
{
    assert(numPixelsToDraw < PIXEL_BUFFER_SIZE);
    assert(drawArea != NULL);

    //NOTE: The ESP-IDF provided interface to the spi peripheral as a horrible mess.
    //Rather than set a few registers transactions are defined via descriptor objects.

    //The current display uses a four-wire interface. SPI lines for CLK, MOSI, CS
    //and an additional D/C line which the display driver uses to determine 
    //whether an SPI transaction data should be treated as DATA or COMMAND.

    //Each transaction requires a transaction descriptor, which will will be 
    //referenced by the IDF framework in the background during spi transmission.
    //The transaction descriptors need to be made static, as the transactions
    //will be queued and continue to execute after this function has finished.
    static spi_transaction_t trans[NUM_SPI_TRANS_FOR_DRAW_TO_SCREEN];
    spi_transaction_t * transResult;
    
    //In order to draw to screen we will need to perform
    //the following SIX spi write operations. We have to
    //break them into seperate transactions in order to 
    //automate control of the D/C pin - which is controlled
    //via a callback which sets the pin to the value specified
    //by the '.user' member of the spi transaction descriptor.

    //trans[0] : CASET CMD (D/C pin LOW)
    trans[0].tx_data[0] = CASET_REG_ADDR;
    trans[0].length = NUM_BITS_IN_BYTE;
    trans[0].flags = SPI_TRANS_USE_TXDATA;
    trans[0].user = SET_DC_PIN_LOW;
    //trans[1] : CASET WRITE PAYLOAD (D/C pin HIGH)
    trans[1].tx_data[0] = (drawArea->xStart >> 8);     //Col START addr MSB
    trans[1].tx_data[1] = drawArea->xStart;            //Col START addr LSB
    trans[1].tx_data[2] = (drawArea->xEnd >> 8);       //Col END addr MSB
    trans[1].tx_data[3] = drawArea->xEnd;              //Col END addr LSB
    trans[1].length = CASET_PAYLOAD_SIZE_IN_BITS;
    trans[1].flags = SPI_TRANS_USE_TXDATA;
    trans[1].user = SET_DC_PIN_HIGH;
    //trans[2] : RASET CMD (D/C pin LOW)
    trans[2].tx_data[0] = RASET_REG_ADDR;
    trans[2].length = NUM_BITS_IN_BYTE;
    trans[2].flags = SPI_TRANS_USE_TXDATA;
    trans[2].user = SET_DC_PIN_LOW;
    //trans[3] : RASET WRITE PAYLOAD (D/C pin HIGH)
    trans[3].tx_data[0] = (drawArea->yStart >> 8);      //Row START addr MSB
    trans[3].tx_data[1] = drawArea->yStart;             //Row START addr LSB
    trans[3].tx_data[2] = (drawArea->yEnd >> 8);        //Row END addr MSB
    trans[3].tx_data[3] = drawArea->yEnd;               //Row END addr LSB
    trans[3].length = RASET_PAYLOAD_SIZE_IN_BITS;
    trans[3].flags = SPI_TRANS_USE_TXDATA;
    trans[3].user = SET_DC_PIN_HIGH;
    //trans[4] : RAMWR CMD (D/C pin LOW)
    trans[4].tx_data[0] = RAMWR_REG_ADDR;
    trans[4].length = NUM_BITS_IN_BYTE;
    trans[4].flags = SPI_TRANS_USE_TXDATA;
    trans[4].user = SET_DC_PIN_LOW;
    //trans[5] : RAMWR WRITE PAYLOAD (D/C pin HIGH)
    trans[5].tx_buffer = pixelBuffer16Bit;
    trans[5].length = numPixelsToDraw * sizeof(*pixelBuffer16Bit) * NUM_BITS_IN_BYTE;
    trans[5].flags = SPI_TRANS_USE_TX_BUFFER;
    trans[5].user = SET_DC_PIN_HIGH;
    trans[5].rxlength = 0;

    //Queue SPI transactions, to be processed immediately by peripheral
    for(uint8_t idx = 0; idx < NUM_SPI_TRANS_FOR_DRAW_TO_SCREEN; idx++)
    {
        assert(spi_device_queue_trans(g_displaySPIHandle, &trans[idx], portMAX_DELAY) == ESP_OK);
    }

    //Horrible but necessary. 
    //Multiple spi transactions are required during a typical
    //display draw operation. Without this the next batch of 
    //transactions may be added to queue before the first batch
    //has finished, which causes data corruption. 
    // Wait for all 6 transactions to be done and get back the results.
    for (int x = 0; x < NUM_SPI_TRANS_FOR_DRAW_TO_SCREEN; x++)
    {
        // The IDF provides a way to check that a previously queued spi
        // transaction has completed - try to replace this with callbacks later
        assert(spi_device_get_trans_result(g_displaySPIHandle, &transResult, portMAX_DELAY) == ESP_OK);
    }
}


//---- Private
static inline void configureSPI(void)
{
    static spi_device_interface_config_t UIg_displaySPIHandle_interface_conf = {};
    static spi_bus_config_t UIg_displaySPIHandle_conf = {};
    static gpio_config_t UIScreenPins_conf = {};

    UIScreenPins_conf.intr_type = GPIO_INTR_DISABLE;
    UIScreenPins_conf.mode = GPIO_MODE_OUTPUT;
    UIScreenPins_conf.pin_bit_mask = DISPLAY_IO_CONFIG_MASK;
    UIScreenPins_conf.pull_down_en = false;
    UIScreenPins_conf.pull_up_en = false;
    gpio_config(&UIScreenPins_conf);

    UIg_displaySPIHandle_conf.miso_io_num = -1;
    UIg_displaySPIHandle_conf.mosi_io_num = DISPLAY_SPI_MOSI_IO;
    UIg_displaySPIHandle_conf.sclk_io_num = DISPLAY_SPI_SCK_IO;
    UIg_displaySPIHandle_conf.quadwp_io_num = -1;
    UIg_displaySPIHandle_conf.quadhd_io_num = -1;
    UIg_displaySPIHandle_conf.max_transfer_sz = PIXEL_BUFFER_SIZE;
    spi_bus_initialize(SCREEN_USING_SPI_INSTANCE, &UIg_displaySPIHandle_conf, SPI_DMA_CH_AUTO);

    UIg_displaySPIHandle_interface_conf.clock_speed_hz = 10 * 1000 * 1000;
    UIg_displaySPIHandle_interface_conf.mode = 0;
    UIg_displaySPIHandle_interface_conf.spics_io_num = DISPLAY_SPI_CS_IO;
    UIg_displaySPIHandle_interface_conf.queue_size = 6;
    UIg_displaySPIHandle_interface_conf.pre_cb = displayPreTransferSPICallback;
    spi_bus_add_device(SCREEN_USING_SPI_INSTANCE, &UIg_displaySPIHandle_interface_conf, &g_displaySPIHandle);
}


//The current display requires a four signal interface, three
//SPI lines and additional DATA/CMD pin. The state of the D/C
//pin determines how the display interprets receieved spi packets.
//---------------------------------------------------------------
//IF the D/C pin is CLEAR the display will iterpret rx spi packet as COMMAND.
//IF the D/C pin is SET the display will interpret rx spi packet as DATA/PARAM
//---------------------------------------------------------------
//This callback is automatically called immediately before a pending SPI
//write is executed (this is a built in optional feature of the ESP-IDF),
//the callback is specified during configuration of the SPI peripheral.
static void displayPreTransferSPICallback(spi_transaction_t * param)
{
    assert(param != NULL);
    gpio_set_level(DISPLAY_DATA_CMD_IO, (uint32_t)param->user);
}

