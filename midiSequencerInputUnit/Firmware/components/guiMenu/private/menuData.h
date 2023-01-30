#include <stdio.h>

#define MENU_SELECTOR_CHAR "o"
#define MENU_ITEM_START_X  50
#define MENU_ITEM_START_Y  60
#define MENU_LINE_MOVE_BY  30
#define MAX_ITEMS_ON_MENU_PAGE 6
#define MENU_MIN_NUMERIC 0
#define MENU_MAX_NUMERIC 127
#define MENU_STRING_MAX_CHARS 8



typedef enum
{
    endOfPages = 0,
    state_base = 1,     //The base state for entire menu
    state_new_project,
    state_load_project,
    state_note_entry,    
} menuPageCode_t;



typedef enum 
{
    param_numeric,              //A single modifiable integer value - the value can be dialed in by the user
    param_string,               //A single modifiable string - the string can be set character-by-charcter by the user
    param_numeric_selection,    //An array of hardcoded integer values, the user makes a selection from the available existing options
    param_string_selection,     //An array of hardcoded strings, the user makes a selection from the avaiable existing options
    param_none                  //Used to identify a selectable menu item which has no assosiated parameter
}parameterType_t;

typedef struct
{
    void * valuePtr;
    uint8_t valMax;
    int8_t  valMin;
    uint16_t posX;
    uint16_t posY;
} param_t;

typedef struct
{
    void * valuePtr;
    void * maxPtr;
    void * minPtr;
    uint16_t posX;
    uint16_t posY;
} param_selection_t;

typedef uint8_t (*functionPtr)(void*);

typedef struct 
{
    menuPageCode_t menuPageCode;
    char * textPtr;
    parameterType_t paramType;
    void * paramPtr;
    menuPageCode_t prevPageCode;
    menuPageCode_t nextPageCode;
    functionPtr funcPtr;
} menuData_t;

extern menuData_t * const menuManagerPtr;

uint8_t setProjectTempoCallback(void * param);
uint8_t setProjectQuantizationCallback(void * param);
uint8_t setProjectNameCallback(void * param);
uint8_t createNewProjectFileCallback(void * param);