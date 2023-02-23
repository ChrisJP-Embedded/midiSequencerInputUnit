
#include <string.h> //We want memset()


#define SCREEN_USING_SPI_INSTANCE SPI2_HOST
#define SCREEN_NUM_X_PIXELS 320 // Remember, screen is mounter with a ccw rotation of 90 degrees (what we call X the display calls Y)
#define SCREEN_NUM_Y_PIXELS 240 // Remember, screen is mounter with a ccw rotation of 90 degrees (what we call Y the display calls X)
#define CHARACTER_SET_NUM_CHARS 62

typedef enum
{
    screenColourWhite = 0x0000,
    screenColourBlack = 0xFFff
} ScreenColour;


typedef enum
{
    px1 = 1,
    px2 = 2,
    px3 = 3,
    px4 = 4
} LineThickness;

typedef enum
{
    fs16,
    fs32
} FontSize;


extern const char characterSet[CHARACTER_SET_NUM_CHARS];


void IPSDisplay_init(void);
uint16_t IPSDisplay_drawLineOfTextToScreen(char *textData, uint16_t len, uint16_t xStart, uint16_t yStart, ScreenColour colour);
void IPSDisplay_drawHorizontalLineToScreen(uint16_t xStart, uint16_t xEnd, uint16_t yPos, LineThickness lineThicknessPx, ScreenColour colour);
void IPSDisplay_drawVerticalLineToScreen(uint16_t yStart, uint16_t yEnd, uint16_t xPos, LineThickness lineThicknessPx, ScreenColour colour);
uint8_t IPSDisplay_getCharWidthInPixels(char charToCheck);
uint8_t IPSDisplay_getCharHeightInPixels(void);
void IPSDisplay_fillScreenWithColour(ScreenColour colour);