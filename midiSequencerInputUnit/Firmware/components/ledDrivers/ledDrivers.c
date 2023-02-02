#include "esp_log.h"
#include "ledDriverPrivates.h"
#include "driver/i2c.h"

#define LOG_TAG                 "LP5862Driver"
#define NUM_LED_DRIVER_ICS      4
#define LED_DRIVER_LATCH_IO     7       
#define LED_DRIVER_0_ADDR       3
#define I2C_MASTER_SCL_IO       2 
#define I2C_MASTER_SDA_IO       1
#define I2C_MASTER_NUM          0
#define I2C_MASTER_FREQ_HZ      1000000
#define I2C_MASTER_TIMEOUT_MS   1000
#define GRID_DEMO_NUM_COLOURS   4

//---- Private ----//
static inline void toggleDriverLatchPins(void);
static inline uint8_t getDriverAddressForTargetColumn(uint8_t columnNum);
static uint8_t I2CLedDriverWrite(uint16_t regAddr, uint8_t *data, uint16_t numBytes, ledDriverICAddr_t deviceAddr, bool isBroadcast);
static esp_err_t configureI2CPeripheral(void);

static bool hasModuleBeenInitialized = false;

//The sequencer grid is made up of 96 switches, arranged into a 6x8 (row x column) matrix.

//Each switch in the grid has its own assosiated RGB LED.
//In order to control/drive all RGB LEDs independently, four Texas Instruments LP5862 RGB driver ICs are used.
//Each of the driver IC's control RGB LEDs for two consecutive columns in the sequencer grid.

//See following docs for more details on the LP5862.
//'LP5862 Datasheet'
//'LP5860 11 × 18 LED Matrix Driver Register Maps' (also applies to LP5862)

//The LP5862 RGB driver ICs are connected to the MCU via a single I2C bus. Hardware configuration of the ICs
//is such that each driver has its own unique I2C address, the unique I2C addresses are specified in the enum
//type 'ledDriverICAddr_t' in the following configuration:

//ledDrvIC0  ->  drives RGB LEDs for column0 (left most column) and column1
//ledDrvIC1  ->  drives RGB LEDs for column2 and column3
//ledDrvIC2  ->  drives RGB LEDs for column4 and column5
//ledDrvIC3  ->  drives RGB LEDs for column6 and column7 (right most column)

//---- IMPORTANT ----//
//Unfortunatelty the I2C peripheral of the ESP32S3 MCU is NOT DMA capable, despite the datasheet 
//claiming that it is (p36/37 datasheet). Therefore all IDF I2C operations are BLOCKING.


//---- Public 
uint8_t ledDrivers_init(void)
{
    //assert(hasModuleBeenInitialized == false);

    uint8_t data[5] = {0};
    gpio_config_t ledDriverLatchPin_conf = {};

    //Latch pin is used to manually trigger
    //latch of PWM register values to outputs
    ledDriverLatchPin_conf.intr_type = GPIO_INTR_DISABLE;
    ledDriverLatchPin_conf.mode = GPIO_MODE_OUTPUT;
    ledDriverLatchPin_conf.pin_bit_mask = (1ULL << LED_DRIVER_LATCH_IO);
    ledDriverLatchPin_conf.pull_down_en = false;
    ledDriverLatchPin_conf.pull_up_en = false;

    if(hasModuleBeenInitialized == false)
    {
        if(gpio_config(&ledDriverLatchPin_conf) != ESP_OK)
        {
            ESP_LOGE(LOG_TAG, "Error: Fault initializing gpio");
            return 1;
        }

        if(configureI2CPeripheral() != ESP_OK)
        {
            ESP_LOGE(LOG_TAG, "Error: Fault initializing I2C periperhal");
            return 1;
        }
    }


    //***********************************
    //Prepare configuration register data
    //***********************************
    //See following docs:
    //'LP5862 Datasheet'
    //'LP5860 11 × 18 LED Matrix Driver Register Maps'

    //Chip ENABLED
    data[0] = 0x01;

    //Dev_initial REG
    //PWM freq = 62.5k
    //Data refresh mode = Mode 2
    //Max scan lines = 10
    data[1] = 0b01010011;    

    //Dev_config1 REG
    //Current sink delay = ON
    //PWM phase shift = ON
    //PWM scale mode = Exponential
    //Line swith blanking = 1us
    data[2] = 0b00000111;

    //Dev_config2 REG
    //All OFF or DISABLED
    data[3] = 0x00;

    //Dev_config3 REG
    //Up deghost enable = ENABLED
    //Max current = 15mA
    //Up deghost = VLED - 2.5V
    //Down deghost = weak deghosting
    data[4] = 0b01010111;
 
    hasModuleBeenInitialized = true;

    //LP586x has auto register increment on writes so we
    //can write the four registers in one I2C transaction
    I2CLedDriverWrite(CHIP_ENABLE_REG_ADDR, data, 5, 0, true);

    return 0;
}



//---- Public
uint8_t ledDrivers_writeSingleLed(uint8_t columnNum, uint8_t rowNum, rgbLedColour_t rgbColourCode)
{
    assert(hasModuleBeenInitialized == true);
    assert((rowNum < SYSTEM_NUM_ROWS) && (columnNum < SYSTEM_NUM_COLUMNS));

    uint8_t lookupIdx;         
    static uint8_t data[NUM_8BIT_PWM_REGISTES_PER_LED] = {0};

    //PWM register addresses are store in the array 'ledDriverPwmAddrRGB'
    //the code below looks up the BASE address of the three PWM
    //registers for the target RGB LED.
    if((columnNum == 0) || (columnNum % 2 == 0))    
    {
        lookupIdx = rowNum * NUM_8BIT_PWM_REGISTES_PER_LED;
    } 
    else
    {
        lookupIdx = (SYSTEM_NUM_ROWS * NUM_8BIT_PWM_REGISTES_PER_LED);
        lookupIdx += (rowNum * NUM_8BIT_PWM_REGISTES_PER_LED);
    }

    //**********************
    //Prepare PWM write data
    //**********************

    //As the RGB PWM registers for any given LED are sequential in memory 
    //we can just address the base PWM register (GREEN) for the target LED
    //as the driver will automatically auto-increment register address
    //as bytes are written

    //The RGB colour codes are 3-bytes, which specify the PWM 
    //values that must be written to generate the specified colour.
    //The most signficant byte of the colour code is the RED PWM value
    //The middle byte of the colour code is the GREEN PWM value
    //The least significant byte of the colour code is the BLUE PWM value

    //Now we need to load the colour code into the data buffer
    //in the required order (to match the order of PWM registers in memory)

    //Green PWM register
    data[0] = (uint8_t)((rgbColourCode & 0x00ff00) >> 8);
    //Red PWM register
    data[1] = (uint8_t)((rgbColourCode & 0xff0000) >> 16);
    //Blue PWM register
    data[2] = (uint8_t)((rgbColourCode & 0x0000ff));

    //ESP_LOGI(LOG_TAG, "idx: %d\n", lookupIdx);
    //ESP_LOGI(LOG_TAG, "PWM BASE: %0x\n", ledDriverPwmAddrRGB[lookupIdx]);

    I2CLedDriverWrite(ledDriverPwmAddrRGB[lookupIdx], data, NUM_8BIT_PWM_REGISTES_PER_LED, getDriverAddressForTargetColumn(columnNum), false);
    toggleDriverLatchPins();  //Need to toggle latch pin before PWM data latched to outputs

    return 0;
}



//---- Public
uint8_t ledDrivers_writeSingleGridColumn(uint8_t columnNum, rgbLedColour_t * columnColoursPtr)
{
    assert(hasModuleBeenInitialized == true);
    assert(columnNum < SYSTEM_NUM_COLUMNS);

    uint8_t lookupIdx = 0;
    uint8_t bufferIdx = 0;
    rgbLedColour_t adjustedColourArr[SYSTEM_NUM_ROWS] = {0};
    uint8_t data[SYSTEM_NUM_ROWS * NUM_8BIT_PWM_REGISTES_PER_LED] = {0};

    //PWM register addresses are store in the array 'ledDriverPwmAddrRGB'
    //the code below looks up the BASE address of the three (R,G,B) PWM
    //registers for the target RGB LED.
    if((columnNum == 0) || (columnNum % 2 == 0)) lookupIdx = 0;    
    else lookupIdx = SYSTEM_NUM_ROWS * NUM_8BIT_PWM_REGISTES_PER_LED;


    //We want to do a single burst write, and rely on the driver IC to auto-increment 
    //its internal write pointer, which will allow us to send all RGB data for a whole 
    //column in a single I2C transaction. 
    
    //Unfortunately, due to PCB routing this won't work properly unless we manually
    //re-order the received colour pwm data array, such that a sequential write accross
    //the drivers interal memory will set the correct colour for each row in the column,
    //we do this here to hide complexity and simplify the interface.
    adjustedColourArr[0] = columnColoursPtr[0];
    adjustedColourArr[1] = columnColoursPtr[5];
    adjustedColourArr[2] = columnColoursPtr[2];
    adjustedColourArr[3] = columnColoursPtr[3];
    adjustedColourArr[4] = columnColoursPtr[4];
    adjustedColourArr[5] = columnColoursPtr[1];

    //Fill byte buffer with colour data..
    for(uint8_t rowNum = 0; rowNum < SYSTEM_NUM_ROWS; ++rowNum)
    {

        //Green PWM register
        data[bufferIdx] = (uint8_t)((adjustedColourArr[rowNum] & 0x00ff00) >> 8);
        bufferIdx++;
        //Red PWM register
        data[bufferIdx] = (uint8_t)((adjustedColourArr[rowNum] & 0xff0000) >> 16);
        bufferIdx++;
        //Blue PWM register
        data[bufferIdx] = (uint8_t)((adjustedColourArr[rowNum] & 0x0000ff));
        bufferIdx++;
    }

    I2CLedDriverWrite(ledDriverPwmAddrRGB[lookupIdx], data, (NUM_8BIT_PWM_REGISTES_PER_LED * SYSTEM_NUM_ROWS), getDriverAddressForTargetColumn(columnNum), false);
    toggleDriverLatchPins();  //Need to toggle latch pin before PWM data latched to outputs

    return 0;
}


//---- Public
uint8_t ledDrivers_writeEntireGrid(rgbLedColour_t * rgbGridColours)
{
    //This function expects a pointer to an array of grid colours,
    //which MUST specifiy a colour code for each LED in the grid.

    //The colour codes specified in the 'rgbGridColours' array
    //are listed in the following order (relating to the grid)
    //row0,col0     (arr[0]), 
    //row0,col1     (arr[1]), 
    //row0,colMax   (arr[colMax])
    //row1,col0     (arr[colMax + 1])
    //Repeated for all rows, for the entire grid.

    assert(hasModuleBeenInitialized == true);
    assert(rgbGridColours != NULL);

    //The data in the recieved array is row by row as described
    //above, as the led drivers control the grid leds on a column
    //basis we will need to extract and update each column
    rgbLedColour_t colourCodesForSingleColumn[SYSTEM_NUM_ROWS];

    //For each column in the grid
    for(uint8_t a = 0; a < SYSTEM_NUM_COLUMNS; ++a)
    {
        //Extract colour for each row in the current column
        for(uint8_t b = 0; b < SYSTEM_NUM_ROWS; ++b)
        {
            colourCodesForSingleColumn[b] = rgbGridColours[(b * SYSTEM_NUM_COLUMNS) + a];
        }

        //Update the led driver controlling the current column
        ledDrivers_writeSingleGridColumn(a, colourCodesForSingleColumn);
    }

    return 0;
}


//---- Public
void ledDrivers_gridTestDemo(void)
{
    //Quick demo to provide visual test all RGB LEDs.
    //Used for development purposes only.

    assert(hasModuleBeenInitialized == true);

    rgbLedColour_t colourArr[GRID_DEMO_NUM_COLOURS] = {rgb_red, rgb_green, rgb_blue, rgb_orange};
    uint8_t colNum;
    uint8_t rowNum;
    uint8_t colourIdx;

    //For each column (left to right), illuminate each row sequentially (top to bottom).
    //Repeat process for each of the colours listed in 'colourArr'.
    for(colourIdx = 0; colourIdx < GRID_DEMO_NUM_COLOURS; colourIdx++)
    {
        for(colNum = 0; colNum < SYSTEM_NUM_COLUMNS; colNum++)
        {
            for(rowNum = 0; rowNum < SYSTEM_NUM_ROWS; rowNum++)
            {
                ledDrivers_writeSingleLed(colNum, rowNum, colourArr[colourIdx]);
                vTaskDelay(pdMS_TO_TICKS(10)); //Slow down so we can see sequence
            }
        }
    }

    //Turn off each previously illuminated LED
    for(colNum = 0; colNum < SYSTEM_NUM_COLUMNS; colNum++)
    {
        for(rowNum = 0; rowNum < SYSTEM_NUM_ROWS; rowNum++)
        {
            ledDrivers_writeSingleLed(colNum, rowNum, rgb_off);
            vTaskDelay(pdMS_TO_TICKS(5)); //Slow down so we can see sequence
        }
    }

    //For each row (top to bottom) illunimate each column sequentially (left to right)
    //Repeat process for each of the colours listed in 'colourArr'.
    for(colourIdx = 0; colourIdx < GRID_DEMO_NUM_COLOURS; colourIdx++)
    {
        for(rowNum = 0; rowNum < SYSTEM_NUM_ROWS; rowNum++)
        {
            for(colNum = 0; colNum < SYSTEM_NUM_COLUMNS; colNum++)
            {
                ledDrivers_writeSingleLed(colNum, rowNum, colourArr[colourIdx]);
                vTaskDelay(pdMS_TO_TICKS(10)); //Slow down so we can see sequence
            }
        }
    }

    //Turn off each previously illuminated LED.
    for(rowNum = 0; rowNum < SYSTEM_NUM_ROWS; rowNum++)
    {
        for(colNum = 0; colNum < SYSTEM_NUM_COLUMNS; colNum++)
        {
            ledDrivers_writeSingleLed(colNum, rowNum, rgb_off);
            vTaskDelay(pdMS_TO_TICKS(5)); //Slow down so we can see sequence
        }
    }
}


//---- Public
void ledDrivers_blankOutEntireGrid(void)
{
    //This function acts to provide a convenient
    //method of blanking all leds in the grid

    uint8_t colNum;
    uint8_t rowNum;

    for(rowNum = 0; rowNum < SYSTEM_NUM_ROWS; rowNum++)
    {
        for(colNum = 0; colNum < SYSTEM_NUM_COLUMNS; colNum++)
        {
            ledDrivers_writeSingleLed(colNum, rowNum, rgb_off);
        }
    }
}


//---- Private
static esp_err_t configureI2CPeripheral(void)
{
    //Setup I2C peripheral for comms with LP5862 led drivers

   assert(hasModuleBeenInitialized == false);

    esp_err_t err = ESP_OK;

    //ESP-IDF provided configuration structure
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,   //We have external, tuned resistors
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ
    };

    //Register the configuration with the peripheral
    err |= i2c_param_config(I2C_MASTER_NUM, &conf);
    err |= i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);

    return err;
}


//---- Private
static uint8_t I2CLedDriverWrite(uint16_t regAddr, uint8_t *data, uint16_t numBytes, ledDriverICAddr_t deviceAddr, bool isBroadcast)
{
    static uint8_t addrByte0;
    static uint8_t addrByte1;
    esp_err_t err;

    assert(hasModuleBeenInitialized == true);

    //The LP586X driver requires two address bytes at the start of each I2C write operation (format explained below).
    //The IDF provided I2C functions allow for a single address byte and a data buffer. In order to fulfil the requirement
    //for two address bytes we need an additional byte to the data buffer, so that data[0] becomes the the second address
    //byte. To do this, we simply allocate a new buffer with space for an extra byte.

    uint8_t * writePayload = calloc(numBytes + 1, sizeof(uint8_t)); //TODO: SWAP OUT FOR STATIC ARRAY LATER
    numBytes++; //The new buffer has an extra byte, so increment numBytes.

    assert(writePayload != NULL);

    //LP6862 IC Expects addrByte0 to have the following format:
    //Bits 7 -> 3 make up the target chip address (remeber, there are multiple led drivers on the I2C bus)
    //Bits 2 -> 1 are in fact the 9th and 8th bits of the hardware register being addressed within the device
    //Bit 0 is the I2C R/W indication bit - this is inserted automatically by the IDF framework.

    //LP6862 IC Expects addrByte1 to have the following format:
    //Bits 7 -> 0 make up bits 7 -> 0 of the 10 bit target register address.

    //---- IMPORTANT ----//
    //The ESP-IDF provided I2C write function 'i2c_master_write_to_device' will 
    //automatically shift addrByte0 by one bit to the left, and insert the I2C R/W bit. 
    //This can cause confusion when reading code if not considered.

    if(isBroadcast)
    {
        //Load chip address for I2C bus broadcast to all drivers
        addrByte0 = (uint8_t)BROADCAST_CHIP_ADDRESS << 2;  
    }
    else
    {
        //Load chip address for specific driver
        addrByte0 = (uint8_t)deviceAddr << 2;
    } 
        
    //OR bits 9 -> 8 of the 10 bit reg address to addrByte0[1] and addrByte0[0] respectively
    addrByte0 |= (uint8_t)((regAddr & 0x0300) >> 8);

    addrByte1 = (uint8_t)regAddr; //Asign bits 7 -> 0 of the 10 bit register address

    //ESP_LOGI(LOG_TAG, "Led driver IC addr: %0x\n", deviceAddr);
    //ESP_LOGI(LOG_TAG, "10 bit register address = %0x\n", regAddr);
    //ESP_LOGI(LOG_TAG, "Computed addrByte0: %0x\n", addrByte0);
    //ESP_LOGI(LOG_TAG, "Computed addrByte1: %0x\n", addrByte1);

    //Load the second address byte into byte 0 of the new data buffer,
    //this way it will be transmitted immediately after the first address byte
    writePayload[0] = addrByte1;

    //Transfer data to new buffer, skip idx 0
    for(uint32_t a = 1; a < numBytes; ++a)
    {
        writePayload[a] = data[a - 1];
    }

    err = i2c_master_write_to_device(I2C_MASTER_NUM, addrByte0, writePayload, numBytes, I2C_MASTER_TIMEOUT_MS);

    if(err != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "Error: I2C write error detected");
        free(writePayload);
        return 1;
    }

    free(writePayload);


    return 0;
}


//---- Private
static inline uint8_t getDriverAddressForTargetColumn(uint8_t columnNum)
{
    //This function returns the I2C address of the 
    //driver IC which controls the target columnNum.
    //Each driver IC controls two columns.

    assert(hasModuleBeenInitialized == true);

    uint8_t driverAddr = 0;

    switch(columnNum)
    {
        case 0: //Left most grid column
        case 1:
            driverAddr = ledDrvIC0;
            break;

        case 2:
        case 3:
            driverAddr = ledDrvIC1;
            break;

        case 4:
        case 5:
            driverAddr = ledDrvIC2;
            break;

        case 6:
        case 7: //Right most grid column
            driverAddr = ledDrvIC3;
            break;
    }

    assert(driverAddr != 0);
    return driverAddr; //Shouldnt get here
}


//---- Private
static inline void toggleDriverLatchPins(void)
{
    //TODO: Automate with timer
    //This pin must be toggled in order for the driver ICs 
    //to latch data from internal SRAM to driver outputs.
    assert(hasModuleBeenInitialized == true);

    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(LED_DRIVER_LATCH_IO, true);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(LED_DRIVER_LATCH_IO, false);
}
