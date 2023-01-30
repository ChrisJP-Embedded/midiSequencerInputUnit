
#include <string.h> //We want memset()


#define SCREEN_USING_SPI_INSTANCE SPI2_HOST
#define SCREEN_NUM_X_PIXELS 320 // Remember, screen is mounter with a ccw rotation of 90 degrees (what we call X the display calls Y)
#define SCREEN_NUM_Y_PIXELS 240 // Remember, screen is mounter with a ccw rotation of 90 degrees (what we call Y the display calls X)
#define CHARACTERSET_MAX_IDX 62

typedef enum
{
    screenColourWhite = 0x0000,
    screenColourBlack = 0xFFff
} sScreenColour_t;


typedef enum
{
    px1 = 1,
    px2 = 2,
    px3 = 3,
    px4 = 4
} sScreenLineThickness_t;

typedef enum
{
    fs16,
    fs32
} eFontSize_t;


extern const char characterSet[CHARACTERSET_MAX_IDX];


void initIPSDisplayDriver(void);
uint16_t drawLineOfTextToScreen(char *textData, uint16_t len, uint16_t xStart, uint16_t yStart, sScreenColour_t colour);
void drawRectangleToScreen(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd, sScreenLineThickness_t linePx, sScreenColour_t colour);
void drawHorizontalLineToScreen(uint16_t xStart, uint16_t xEnd, uint16_t yPos, sScreenLineThickness_t lineThicknessPx, sScreenColour_t colour);
void drawVerticalLineToScreen(uint16_t yStart, uint16_t yEnd, uint16_t xPos, sScreenLineThickness_t lineThicknessPx, sScreenColour_t colour);
uint8_t getCharWidthInPixels(char c);
uint8_t getCharHeightInPixels(void);

void fillScreenWithColour(sScreenColour_t colour);