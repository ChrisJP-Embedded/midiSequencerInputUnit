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
    endOfPages,
    state_base,
    state_new_project,
    state_load_project,
    state_grid_edit,
    state_note_edit,    
} menuPageCode_t;

typedef enum 
{
    param_numeric,              //A single modifiable integer value - the value can be dialed in by the user
    param_string,               //A single modifiable string - the string can be set character-by-charcter by the user
    param_numeric_selection,    //An array of hardcoded integer values, the user makes a selection from the available existing options
    param_string_selection,     //An array of hardcoded strings, the user makes a selection from the avaiable existing options
    param_none                  //Used to identify a selectable menu item which has no assosiated parameter
} menuParamType_t;


typedef struct
{
    void * valuePtr;
    uint8_t valMin;
    uint8_t valMax;
    uint16_t posX;
    uint16_t posY;
} MenuParam;


typedef struct
{
    void ** valuePtr;
    uint8_t numItems;
    uint8_t currIdx;
    uint16_t posX;
    uint16_t posY;
} MenuParamSelection;


typedef struct 
{
    menuPageCode_t menuPageCode;
    char * textPtr;
    menuParamType_t paramType;
    void * paramPtr;
    menuPageCode_t prevPageCode;
    menuPageCode_t nextPageCode;
    uint8_t (*funcPtr)(void*);
} menuData_t;


extern menuData_t * const menuManagerPtr;


uint8_t createNewProjectFileCallback(void * param);
uint8_t updateNoteVelocity(void * param);
uint8_t updateNoteDuration(void * param);