#include <stdio.h>
#include <esp_log.h>
#include "private/menuData.h"
#include "include/guiMenu.h"
#include "ipsDisplay.h"
#include "rotaryEncoders.h"

#define LOG_TAG "menuSystem"
#define MAX_PROJECT_NAME_LENGTH     8
#define MAX_MENU_DATA_ITEMS         8
#define MAX_STRING_CHARS            30
#define SELECTION_INDICATOR_PIXEL_XOFFSET 30

//---- Private ----//
static void updateMenuPage(void);
static void moveSelectionIndicator(bool isUpOrDown);
static void resetMenuIndicator(void);
static void processMenuUserInput(uint8_t eventByte);
static void editMenuItemParam(parameterType_t paramType);
static void editNumericSelectionParam(param_selection_t *paramPtr);
static void editStringSelectionParam(param_selection_t *paramPtr);
static void editNumericParam(param_t *paramPtr);
static void editStringParam(param_t *paramPtr);
static uint8_t createDefaultProjectName(void * unusedParam);


//This struct type holds all data relating the to the menu
//selection indicator. Each menu page is list of items,
//the indicator shows the currently selected menu item.
typedef struct
{
    const uint16_t xStartPos;   //Selection indicatior only moves verticaly, horizontal position remains constant
    uint16_t yPosCurrent;       //After a page is loaded the selector only moves along the y axis
    uint8_t currentItem;        //Index of the currently selected item within the current menu page
    void (*moveSelectionIndicator)(bool isUpOrDown); //Shifts position of the selector along y axis to next/prev menu item (if exists)
    void (*resetMenuIndicator)(void); //This is called to reset the position of the selector as each new menu page is loaded
} MenuSelectionIndicator_t;


//This struct type holds all menu navigation
//data, including the selection indicator.
typedef struct
{
    //Menu item strings and runtime paramaters are stored in an array of structures (declared in 'g_MenuData.c'), 
    //where a single struct holds all display and parameter data relating to a single menu item. 
    //A single menu page may constist of several consectutive structs.
    menuPageCode_t pageCode; //Each array element has a 'pageCode' member. A group of entries relating to a single menu page share a common page code
    uint8_t menuPageBaseIdx; //Holds the base array index of the current menu page (determined by looking up the 'pageCode')
    uint8_t selectableItems; //Holds the number of selectable items in the current menu page (determined by number of elements sharing same pageCode)
    bool updateMenuPageFlag; //Set when the 'pageCode' has been updated and the menu needs to be refreshed
    MenuSelectionIndicator_t selectionIndicator; //Container for all data relating to the selection indicator
}  MenuRuntimeData_t;


//Initialize local menu data cache
static MenuRuntimeData_t g_MenuData = {
    .pageCode = state_base, 
    .menuPageBaseIdx = 0,
    .selectableItems = 0,
    .updateMenuPageFlag = true,
    {
        .xStartPos = (uint16_t)(MENU_ITEM_START_X - SELECTION_INDICATOR_PIXEL_XOFFSET),
        .yPosCurrent = MENU_ITEM_START_Y, 
        .currentItem = 0, 
        .moveSelectionIndicator = &moveSelectionIndicator, 
        .resetMenuIndicator = &resetMenuIndicator
    }
};


//The menu uses 'g_MenuEventReturn' as a return value to the system,
//it indicates any events and assosiated data which should be handled 
//by the system. This is a means to enable menu driven events.
MenuEventData_t g_MenuEventReturn = {.eventData = NULL, .eventOpcode = 0};


//The menu system requires access to an up to date record of the number of files on the system and their
//respective filenames. The pointers below are set at startup and point directly to locations managed by 
//the file system, which are automatically refreshed as file system operations are performed.
static const uint8_t * g_numFileNamesPtr = NULL; //points to the current number of files on the file system
static const char ** g_fileNamesPtr = NULL; //points to an array of strings, which are the names of all files


//In order to allow for users to create and edit filenames a local cache
//is required, this will need to exist throughout the program lifetime.
static char g_projectNameArr[MAX_PROJECT_NAME_LENGTH + 1]; //Add one for string termination




//---- Public
void guiMenu_init(const char * fileNamesPtr[], const uint8_t * const numFilesOnSystem)
{
    //Initializes the menu module, this MUST be 
    //called before the module runtime interface

    //The menu module needs access to the current
    //number of files and their respective names
    g_numFileNamesPtr = numFilesOnSystem;
    g_fileNamesPtr = (const char **)fileNamesPtr;

    //When the system starts the callbacks need to be assigned
    //TODO: Refine callback assignment once system defined
    menuManagerPtr[0].funcPtr = createDefaultProjectName;

    IPSDisplay_fillScreenWithColour(screenColourBlack);
    updateMenuPage();
    resetMenuIndicator();
}

//---- Public
MenuEventData_t guiMenu_interface(uint8_t menuInputBuffer)
{
    //This function provides the runtime interface to the menu module,
    //an event code is passed as input which acts to drive the menu.

    //We need to reset the return object  
    //as it will still hold previous values.
    //As the input is processed these values
    //may be set by menu system callbacks.
    g_MenuEventReturn.eventOpcode = 0;
    g_MenuEventReturn.eventData = NULL;

    //TODO: add range check for input once fully defined
    processMenuUserInput(menuInputBuffer);

    //This flag is set TRUE on system start, after that it is set locally as
    //a result by 'processMenuUserInput' if the gui needs to be updated
    if(g_MenuData.updateMenuPageFlag == true)
    {
        g_MenuData.updateMenuPageFlag = false;
        IPSDisplay_fillScreenWithColour(screenColourBlack);
        updateMenuPage();
        resetMenuIndicator();
    }

    return g_MenuEventReturn;
}




//---- Private
static void processMenuUserInput(uint8_t eventByte)
{
    //This function is called when user input has been recieved via
    //the event queue - the event is processed accordingly

    bool callItemFuncPtr = false;
    param_t *paramPtr = NULL;
    parameterType_t paramType = menuManagerPtr[g_MenuData.menuPageBaseIdx + g_MenuData.selectionIndicator.currentItem].paramType;

    switch (eventByte)
    {
        case encoder0_cw:
            break;


        case encoder0_ccw:
            break;


        case encoder0_sw:
            //Page back button pressed
            if(menuManagerPtr[g_MenuData.menuPageBaseIdx + g_MenuData.selectionIndicator.currentItem].prevPageCode != 0)
            {
                g_MenuData.pageCode = menuManagerPtr[g_MenuData.menuPageBaseIdx + g_MenuData.selectionIndicator.currentItem].prevPageCode;
                g_MenuData.updateMenuPageFlag = true;
            }
            break;


        case encoder1_cw:
            moveSelectionIndicator(false);
            break;


        case encoder1_ccw:
            moveSelectionIndicator(true);
            break;


        case encoder1_sw:
            callItemFuncPtr = true;

            if (paramType == param_string || paramType == param_numeric || paramType == param_string_selection || paramType == param_numeric_selection)
            {
                //Abort if param pointer is NULL (meaning no parameter is currently set up)
                if(menuManagerPtr[g_MenuData.menuPageBaseIdx + g_MenuData.selectionIndicator.currentItem].paramPtr == NULL) break;
                //Need to engage editing for a string parameter.
                editMenuItemParam(paramType);
            }
            else
            {
                //Doesn't have an assosiated parameter, does it indicate a state change?
                if(menuManagerPtr[g_MenuData.menuPageBaseIdx + g_MenuData.selectionIndicator.currentItem].nextPageCode != 0)
                {
                    g_MenuData.pageCode = menuManagerPtr[g_MenuData.menuPageBaseIdx + g_MenuData.selectionIndicator.currentItem].nextPageCode;
                    g_MenuData.updateMenuPageFlag = true;
                }
            }
            break;
    }


    //---- MANAGE MENU EVENT CALLBACKS ----//
    //If the menu item thats just been selected has an assosiated event callback, then execute it
    if((menuManagerPtr[g_MenuData.menuPageBaseIdx + g_MenuData.selectionIndicator.currentItem].funcPtr != NULL) && callItemFuncPtr)
    {
        callItemFuncPtr = false;

        paramPtr = (param_t *)menuManagerPtr[g_MenuData.menuPageBaseIdx + g_MenuData.selectionIndicator.currentItem].paramPtr;

        if(paramPtr != NULL)
        {
            menuManagerPtr[g_MenuData.menuPageBaseIdx + g_MenuData.selectionIndicator.currentItem].funcPtr(paramPtr->valuePtr);
        }
        else
        {
            menuManagerPtr[g_MenuData.menuPageBaseIdx + g_MenuData.selectionIndicator.currentItem].funcPtr(NULL);
        }
    }
}


//---- Private
static void updateMenuPage(void)
{
    //This function is called when a new menu page must be drawn to the display
    //this happens automatically on system startup (to draw front page of menu)
    //and as a result of the user navigating to a new menu page.

    char stringCharArray[MAX_STRING_CHARS];             //Used to construct temp strings
    uint8_t currentMenuItemIdx = 0;                     //Acts an an index for 'menuManagerPtr'
    uint16_t displayPosY = MENU_ITEM_START_Y;           //Display y coordinate store
    uint16_t displayPosX;                               //Display x coordinate store
    uint8_t stringLen;                                  //Store for calculated string lengths

    param_t * paramPtr = NULL;                          //Used to reference any assosiated menu item parameters
    param_selection_t *paramSelectionPtr = NULL;        //Used to reference any assosiated meny item selection parameters

    //We need to lookup the base idx for the requested menu page, we do this by
    //searching for the first entry in the array which has a matching page code
    //note: menu pages with multiple menu items all share the same page code and
    //are always stored sequentially within the menu data array.

    while(menuManagerPtr[currentMenuItemIdx].menuPageCode != g_MenuData.pageCode)
    {
        currentMenuItemIdx++;
        assert(currentMenuItemIdx <= MAX_MENU_DATA_ITEMS);
    } 
 
    //Store the base IDX for the menus page data
    g_MenuData.menuPageBaseIdx = currentMenuItemIdx;
    g_MenuData.selectableItems = 0;   //Clear prev value

    //We previously identified the base IDX for the requested menu page data, this loop will process
    //all menu data for the requested page - starting at the base IDX, and conitinuing sequentially
    //while the menu items menu page code matches that of the requested page code.
    while (menuManagerPtr[currentMenuItemIdx].menuPageCode == g_MenuData.pageCode)
    {
        g_MenuData.selectableItems++;
        stringLen = (uint16_t)strlen(menuManagerPtr[currentMenuItemIdx].textPtr);
        displayPosX = MENU_ITEM_START_X;
        displayPosX += IPSDisplay_drawLineOfTextToScreen(menuManagerPtr[currentMenuItemIdx].textPtr, 
                                                        stringLen, MENU_ITEM_START_X, displayPosY, screenColourWhite);

        if (menuManagerPtr[currentMenuItemIdx].paramType != param_none) //Skip if current menu item has no assosiated parameter
        {
            //This menu item has an assosiated parameter
            //The param can be either a string or integer
            switch (menuManagerPtr[currentMenuItemIdx].paramType)
            {
                case param_numeric:
                    //If param pointer is NULL then abort!!
                    if ((menuManagerPtr[currentMenuItemIdx].paramPtr) == NULL) break;
                    //Grab direct pointer to the parameter struct
                    paramPtr = (param_t *)menuManagerPtr[currentMenuItemIdx].paramPtr;
                    //Turn the numeric param into a string to be displayed
                    stringLen = snprintf(NULL, 0, "%d", *(uint8_t*)paramPtr->valuePtr);
                    memset(stringCharArray, 0, MAX_STRING_CHARS);
                    snprintf(stringCharArray, stringLen + 1, "%d", *(uint8_t*)paramPtr->valuePtr);
                    IPSDisplay_drawLineOfTextToScreen(stringCharArray, stringLen, displayPosX, displayPosY, screenColourWhite);
                    //Save the parameter display co-ordinates,
                    //they'll be required when user editing the param
                    paramPtr->posX = displayPosX;
                    paramPtr->posY = displayPosY;
                    break;

                case param_string:
                    //If param pointer is NULL then abort!!
                    if ((menuManagerPtr[currentMenuItemIdx].paramPtr) == NULL) break;
                    //Grab direct pointer to the parameter struct
                    paramPtr = (param_t *)menuManagerPtr[currentMenuItemIdx].paramPtr;
                    //string does exist, so push it to the display
                    stringLen = (uint16_t)strlen(((char*)paramPtr->valuePtr));
                    IPSDisplay_drawLineOfTextToScreen(((char*)paramPtr->valuePtr), stringLen, displayPosX, displayPosY, screenColourWhite);
                    //Save the parameter display co-ordinates,
                    //they'll be required when user editing the param
                    paramPtr->posX = displayPosX;
                    paramPtr->posY = displayPosY;
                    break;

                case param_numeric_selection:
                    //If param pointer is NULL then abort!!
                    if ((menuManagerPtr[currentMenuItemIdx].paramPtr) == NULL) break;
                    //Grab direct pointer to the parameter struct
                    paramSelectionPtr = (param_selection_t *)menuManagerPtr[currentMenuItemIdx].paramPtr;
                    //Turn the numeric param into a string to be displayed
                    stringLen = snprintf(NULL, 0, "%d", *(uint8_t*)paramSelectionPtr->valuePtr);
                    memset(stringCharArray, 0, MAX_STRING_CHARS);
                    if(stringCharArray == NULL) return; //** ADD ERROR HANDLING **
                    snprintf(stringCharArray, stringLen + 1, "%d", *(uint8_t*)paramSelectionPtr->valuePtr);
                    IPSDisplay_drawLineOfTextToScreen(stringCharArray, stringLen, displayPosX, displayPosY, screenColourWhite);
                    paramSelectionPtr->posX = displayPosX;
                    paramSelectionPtr->posY = displayPosY;
                    break;

                case param_string_selection:
                    //If param pointer is NULL then abort!!
                    if ((menuManagerPtr[currentMenuItemIdx].paramPtr) == NULL) break;
                    //Grab direct pointer to the parameter struct
                    paramSelectionPtr = (param_selection_t *)menuManagerPtr[currentMenuItemIdx].paramPtr;
                    //For selections, we always init to the 0 idx of the selection
                    stringLen = (uint16_t)strlen(*(char**)paramSelectionPtr->valuePtr);
                    IPSDisplay_drawLineOfTextToScreen(*(char**)paramSelectionPtr->valuePtr, stringLen, displayPosX, displayPosY, screenColourWhite);
                    //Save the parameter display co-ordinates,
                    //they'll be required when user editing the param
                    paramSelectionPtr->posX = displayPosX;
                    paramSelectionPtr->posY = displayPosY;
                    break;

                default:
                    break;
            }
        }

        displayPosY += MENU_LINE_MOVE_BY;   //Move y position onto next line of text
        currentMenuItemIdx++;
    }
}




//---- Private
void editMenuItemParam(parameterType_t paramType)
{
    //This function is called when a menu item with an editable assosiated parameter
    //has been selected for edit. It acts to provide a direct pointer to the parameter
    //before selecting the appropriate edit process, based on the paramater type.

    //Grab direct pointer to param, used void* as params have various types.
    void *paramPtr = menuManagerPtr[g_MenuData.menuPageBaseIdx + g_MenuData.selectionIndicator.currentItem].paramPtr;
    assert(paramPtr != NULL);

    //While the parameter editing processes are similar in some ways,
    //each is unique enough to warrnt its own process.
    switch (paramType)
    {
        case param_numeric:
            //ESP_LOGI(LOG_TAG, "menu param numeric\n");
            editNumericParam(paramPtr);
            break;

        case param_string:
            //ESP_LOGI(LOG_TAG, "menu param string\n");
            editStringParam(paramPtr);
            break;

        case param_string_selection:
            //ESP_LOGI(LOG_TAG, "menu param string selection\n");
            editStringSelectionParam(paramPtr);
            break;

        case param_numeric_selection:
            //ESP_LOGI(LOG_TAG, "menu param numeric selection\n");
            editNumericSelectionParam(paramPtr);
            break;

        default:
            break;
    }
}


//---- Private
void editNumericSelectionParam(param_selection_t *paramPtr)
{
    //This function allows the user to edit a numeric selection, which is a pre-defined selection of numeric values.
    //The selection edit process loop is driven by queued user input events, sent from the 'RotaryEncoders' component.
    assert(paramPtr != NULL);

    char stringCharArray[MAX_STRING_CHARS];             //Used to construct temp strings 
    int8_t previousValue = 0;           //Holds previous selection value so it can be erased from display
    uint8_t receivedQueueItem;          //Used to store receieved event queue item 
    uint16_t stringLen;                 //Used to store calculated string lengths


    struct {    //Flags used by process
        uint8_t exitEditProcess : 1;            //Set when user exists numeric edit process
        uint8_t selectionHasBeenModified : 1;   //Set when numeric value has been changed by user
    } flags;

    //---- CLEAR FLAGS ----
    flags.exitEditProcess = 0;
    flags.selectionHasBeenModified = 0;
    //-------------------------------------------
    //---- Enter selection edit process loop ----
    //-------------------------------------------
    while (1)
    {
        //Encoder1 (sw) is used to exit the numeric edit process once editing is complete.
        //Encoder1 (cw,ccw) (increment, decrement) used to modify value of numeric param.

        //If user input event has occured a new
        //queue item will appear in this queue.. 
        if (uxQueueMessagesWaiting(g_EncodersQueueHandle))
        {
            //Get the next item in the queue, put in 'menuInputBuffer'
            xQueueReceive(g_EncodersQueueHandle, &receivedQueueItem, 0);

            switch (receivedQueueItem)
            {
                //Switch event
                case encoder1_sw:
                    flags.exitEditProcess = 1;
                    break;

                //Clockwise event
                case encoder1_cw:
                    //Check to make sure selection pointer 
                    //remains within bounds of selectable values
                    if (paramPtr->valuePtr < paramPtr->maxPtr)
                    {
                        flags.selectionHasBeenModified = 1;
                        previousValue = *(int8_t *)paramPtr->valuePtr;
                        paramPtr->valuePtr += sizeof(int8_t);
                    }
                    break;

                //Counter-clockwise event
                case encoder1_ccw:
                    //Check to make sure selection pointer 
                    //remains within bounds of selectable values
                    if (paramPtr->valuePtr > paramPtr->minPtr)
                    {
                        flags.selectionHasBeenModified = 1;
                        previousValue = *(int8_t *)paramPtr->valuePtr;
                        paramPtr->valuePtr -= sizeof(int8_t);
                    }
                    break;
            }
        }

        if (flags.selectionHasBeenModified)
        {
            flags.selectionHasBeenModified = 1;

            //Convert old value to string so we can remove it
            //from the display by painting over it in black
            stringLen = snprintf(NULL, 0, "%d", previousValue);
            memset(stringCharArray, 0, MAX_STRING_CHARS);
            snprintf(stringCharArray, stringLen + 1, "%d", previousValue);

            //Remove the previous value from display
            IPSDisplay_drawLineOfTextToScreen(stringCharArray, stringLen, paramPtr->posX, paramPtr->posY, screenColourBlack);

            //Convert new value to string, so we can update display
            stringLen = snprintf(NULL, 0, "%d", (*(uint8_t *)paramPtr->valuePtr));
            memset(stringCharArray, 0, MAX_STRING_CHARS);
            snprintf(stringCharArray, stringLen + 1, "%d", (*(uint8_t *)paramPtr->valuePtr));

            //Draw the new value on display
            IPSDisplay_drawLineOfTextToScreen(stringCharArray, stringLen, paramPtr->posX, paramPtr->posY, screenColourWhite);
        }

        if(flags.exitEditProcess) break;

        vTaskDelay(1);
    }
}


//---- private
void editStringSelectionParam(param_selection_t *paramPtr)
{
    //This function allows the user to edit a string selection, which is a pre-defined selection of strings.
    //The selection edit process loop is driven by queued user input events, sent from the 'RotaryEncoders' component.
    assert(paramPtr != NULL);

    char *previousStringPtr = NULL;     //Stores address of previous selection so it can be erased from display
    uint8_t receivedQueueItem;          //Used to store receieved event queue item 
    uint16_t stringLen;                 //Used to store calculated string lengths

    struct {    //Flags used by process
        uint8_t exitEditProcess : 1;            //Set when user exists numeric edit process
        uint8_t selectionHasBeenModified : 1;   //Set when numeric value has been changed by user
    } flags;

    //---- CLEAR FLAGS ----
    flags.exitEditProcess = 0;
    flags.selectionHasBeenModified = 0;
    //-------------------------------------------
    //---- Enter selection edit process loop ----
    //-------------------------------------------
    while (1)
    {
        //Encoder1 (sw) is used to exit the selection edit process once editing is complete.
        //Encoder1 (cw,ccw) used to scroll through the available values in the selection.

        //If user input event has occured a new
        //queue item will appear in this queue.. 
        if (uxQueueMessagesWaiting(g_EncodersQueueHandle))
        {
            //Get the next item in the queue, put in 'menuInputBuffer'
            xQueueReceive(g_EncodersQueueHandle, &receivedQueueItem, 0);

            switch (receivedQueueItem)
            {
                //Switch event
                case encoder1_sw:
                    flags.exitEditProcess = 1;
                    break;

                //Clockwise event
                case encoder1_cw:
                    //Check to make sure selection pointer 
                    //remains within bounds of selectable values
                    if (paramPtr->valuePtr < paramPtr->maxPtr)
                    {
                        flags.selectionHasBeenModified = 1;
                        previousStringPtr = *(char **)paramPtr->valuePtr;
                        //ESP_LOGI(LOG_TAG, "previous string %s\n", previousStringPtr);
                        paramPtr->valuePtr += sizeof(char*);
                        //ESP_LOGI(LOG_TAG, "new 'paramPtr->valuePtr': %p\n", paramPtr->valuePtr);
                        //ESP_LOGI(LOG_TAG, "maxPtr: %p\n", paramPtr->maxPtr);
                        //ESP_LOGI(LOG_TAG, "new string %s\n", *(char**)paramPtr->valuePtr);
                    }
                    break;

                //Counter-clockwise event
                case encoder1_ccw:
                    //Check to make sure selection pointer 
                    //remains within bounds of selectable values
                    if (paramPtr->valuePtr > paramPtr->minPtr)
                    {
                        flags.selectionHasBeenModified = 1;
                        previousStringPtr = *(char **)paramPtr->valuePtr;
                        //ESP_LOGI(LOG_TAG, "previous string %s\n", previousStringPtr);
                        paramPtr->valuePtr -= sizeof(char*);
                        //ESP_LOGI(LOG_TAG, "new 'paramPtr->valuePtr': %p\n", paramPtr->valuePtr);
                        //ESP_LOGI(LOG_TAG, "maxPtr: %p\n", paramPtr->maxPtr);
                        //ESP_LOGI(LOG_TAG, "new string %s\n", *(char**)paramPtr->valuePtr);
                    }
                    break;
            }
        }

        if (flags.selectionHasBeenModified)
        {
            flags.selectionHasBeenModified = 0;
            
            //Get length of previous string selection
            stringLen = snprintf(NULL, 0, "%s", (char *)previousStringPtr);
            //Remove previous string by painting over it in background colour
            IPSDisplay_drawLineOfTextToScreen((char *)previousStringPtr, stringLen, paramPtr->posX, paramPtr->posY, screenColourBlack);

            //Get length of new string selection 
            stringLen = snprintf(NULL, 0, "%s", *(char **)paramPtr->valuePtr);
            //Draw new value on display in foreground colour
            IPSDisplay_drawLineOfTextToScreen(*(char **)paramPtr->valuePtr, stringLen, paramPtr->posX, paramPtr->posY, screenColourWhite);
        }

        if (flags.exitEditProcess) break;

        vTaskDelay(1);  //Smash the idle task to re-set watchdg
    }
}


//---- Private 
void editNumericParam(param_t *paramPtr)
{
    //This function handles editing of a menu numeric parameter, such that the used can dial in integer values.
    //The numeric edit process loop is driven by queued user input events, sent from the 'RotaryEncoders' component.
    assert(paramPtr != NULL);

    char stringCharArray[MAX_STRING_CHARS];             //Used to construct temp strings 
    uint8_t previousParamValue = 0; //Used to erase previous value from display
    uint8_t receivedQueueItem;      //Used to store receieved event queue item
    uint16_t stringLen;             //Used to store calculated string lengths

    struct {    //Flags used by process
        uint8_t exitNumericEditProcess : 1; //Set when user exists numeric edit process
        uint8_t numericHasBeenEdited : 1;   //Set when numeric value has been changed by user
    } flags;

    //---- CLEAR FLAGS ----
    flags.exitNumericEditProcess = 0;
    flags.numericHasBeenEdited = 0;
    //-----------------------------------------
    //---- Enter numeric edit process loop ----
    //-----------------------------------------
    while (1)
    {
        //Encoder1 (sw) is used to exit the numeric edit process once editing is complete.
        //Encoder1 (cw,ccw) (increment, decrement) used to modify value of numeric param.

        //If user input event has occured a new
        //queue item will appear in this queue.. 
        if (uxQueueMessagesWaiting(g_EncodersQueueHandle))
        {
            //Get the next item in the queue, put in 'menuInputBuffer'
            xQueueReceive(g_EncodersQueueHandle, &receivedQueueItem, 0);

            switch (receivedQueueItem)
            {
                //Switch event
                case encoder1_sw:
                    flags.exitNumericEditProcess = 1;
                    break;

                //Clockwise event
                case encoder1_cw:
                    //Check to make sure value remains within bounds
                    if (*(uint8_t *)paramPtr->valuePtr < paramPtr->valMax)
                    {
                        flags.numericHasBeenEdited = 1;
                        previousParamValue = *(uint8_t *)paramPtr->valuePtr;
                        //ESP_LOGI(LOG_TAG, "previousValue: %d\n", previousParamValue);
                        //Value is within bounds so increment..
                        *(uint8_t *)paramPtr->valuePtr += sizeof(uint8_t);
                        //ESP_LOGI(LOG_TAG, "new value: %d\n", *(uint8_t*)paramPtr->valuePtr);
                    }
                    break;

                //Counter-clockwise event
                case encoder1_ccw:
                    //Check to make sure value remains within bounds
                    if (*(uint8_t *)paramPtr->valuePtr > paramPtr->valMin)
                    {
                        flags.numericHasBeenEdited = 1;
                        previousParamValue = *(uint8_t *)paramPtr->valuePtr;
                        //ESP_LOGI(LOG_TAG, "previousValue: %d\n", previousParamValue);
                        //Value is within bounds to decrement..
                        *(uint8_t *)paramPtr->valuePtr -= sizeof(uint8_t);
                        //ESP_LOGI(LOG_TAG, "new value: %d\n", *(uint8_t*)paramPtr->valuePtr);
                    }
                    break;
            }
        }

        if(flags.numericHasBeenEdited)
        {
            flags.numericHasBeenEdited = 0;

            //Convert old value to string so we can remove it
            //from the display by painting over it in black
            stringLen = snprintf(NULL, 0, "%d", previousParamValue);
            memset(stringCharArray, 0, MAX_STRING_CHARS);

            snprintf(stringCharArray, stringLen + 1, "%d", previousParamValue);
            //Remove the previous value from display by painting over in background colour
            IPSDisplay_drawLineOfTextToScreen(stringCharArray, stringLen, paramPtr->posX, paramPtr->posY, screenColourBlack);


            //Convert new value to string, so we can update display
            stringLen = snprintf(NULL, 0, "%d", (*(uint8_t *)paramPtr->valuePtr));
            memset(stringCharArray, 0, MAX_STRING_CHARS);

            snprintf(stringCharArray, stringLen + 1, "%d", (*(uint8_t *)paramPtr->valuePtr));
            //Draw the new value on display in foreground colour
            IPSDisplay_drawLineOfTextToScreen(stringCharArray, stringLen, paramPtr->posX, paramPtr->posY, screenColourWhite);
        }

        if (flags.exitNumericEditProcess) break;

        vTaskDelay(1);  //Smash the idle task to re-set watchdg
    }
}


//---- Private
void editStringParam(param_t *paramPtr)
{
    //This function handles editing of a menu string parameter, such that the user can enter/edit project names etc. 
    //The string edit process loop is driven by queued user input events, sent from the 'RotaryEncoders' component.
    assert(paramPtr != NULL);

    char stringCharArray[MAX_STRING_CHARS];     //Used to construct temp strings 
    uint8_t characterSetIdx = 0;                //Used to index the available system character set
    uint8_t editStringIdx = 0;                  //Used to store index of character selected for edit within string param
    uint16_t editMarkerPosXOffset = 0;          //X offset in pixels, used to place selected character underline
    uint8_t receivedInputEvent = 0;             //Used to store receieved event queue item
    uint16_t stringLen;                         //Used as a store for calculated string lengths   

    struct {    //Flags used by process
        uint8_t exitStringEditProcess : 1;      //Set when user exists the string edit process
        uint8_t stringHasBeenModified : 1;      //Set when the currently selected character is changed by user
    } flags;


    //Find index of the first charcter of the
    //string within the available character set array
    while (characterSet[characterSetIdx] != *(char *)paramPtr->valuePtr)
    {
        characterSetIdx++;
        assert(characterSetIdx != (CHARACTER_SET_NUM_CHARS - 1));
    }

    //The string edit process always starts up
    //with the first character selected for edit
    editMarkerPosXOffset = paramPtr->posX;

    //Underline character being editing (always inits on character at idx 0 in the string)
    IPSDisplay_drawHorizontalLineToScreen(editMarkerPosXOffset, editMarkerPosXOffset + IPSDisplay_getCharWidthInPixels(*(char *)paramPtr->valuePtr), 
                                        paramPtr->posY + IPSDisplay_getCharHeightInPixels(), 2, screenColourWhite);


    //---- CLEAR FLAGS ----
    flags.exitStringEditProcess = 0;
    flags.stringHasBeenModified = 0;
    //----------------------------------------
    //---- Enter string edit process loop ----
    //----------------------------------------

    while(1)   
    {
        //Encoder0 (cw,ccw) is used to select the current character for edit within the string parameter.
        //Encoder1 (cw,ccw) is used to scroll through the character set, for the character selected for edit by encoder0.
        //Encoder1 (sw) is used to exit the string edit process once editing is complete.

        //If user input event has occured a new
        //queue item will appear in this queue.. 
        if (uxQueueMessagesWaiting(g_EncodersQueueHandle))
        {
            //Grab the new item from the queue so it can be processed
            xQueueReceive(g_EncodersQueueHandle, &receivedInputEvent, 0);

            switch (receivedInputEvent)
            {
                //Clockwise event
                case encoder0_cw: 
                    //Get the current length of string being edited
                    stringLen = strlen((char*)paramPtr->valuePtr);
                    //The if statement below will allow one extra character to be appended at
                    //a time, up to a maximum of 'MENU_STRING_MAX_CHARS' characters in length
                    if(editStringIdx < (MAX_PROJECT_NAME_LENGTH-1))
                    {
                        //erase present character selection underline
                        IPSDisplay_drawHorizontalLineToScreen(editMarkerPosXOffset, 
                            editMarkerPosXOffset + IPSDisplay_getCharWidthInPixels(*((char *)paramPtr->valuePtr + (sizeof(char) * editStringIdx))), 
                            paramPtr->posY + IPSDisplay_getCharHeightInPixels(), 2, screenColourBlack);


                        //Update X offset for the placement of the underline to its new selection position
                        editMarkerPosXOffset += IPSDisplay_getCharWidthInPixels(*((char *)paramPtr->valuePtr + (sizeof(char) * editStringIdx))) + 1;
                        editStringIdx++; //Update the index of character currently being edited to new selection

                        //ESP_LOGI(LOG_TAG, "editMarkerPosXOffset: %d", editMarkerPosXOffset);
                        //ESP_LOGI(LOG_TAG, "editStringIdx: %d", editStringIdx);
                        //ESP_LOGI(LOG_TAG, "%0x %c", (uint8_t)(*((char *)paramPtr->valuePtr + (sizeof(char) * editStringIdx))), 
                            //*((char *)paramPtr->valuePtr + (sizeof(char) * editStringIdx)));

                        //Draw new character selection underline
                        if((uint8_t)(*((char *)paramPtr->valuePtr + (sizeof(char) * editStringIdx))) == 0)
                        {
                            characterSetIdx = 0;
                            flags.stringHasBeenModified = true;   
                        }
                        
                        IPSDisplay_drawHorizontalLineToScreen(editMarkerPosXOffset, 
                            editMarkerPosXOffset + IPSDisplay_getCharWidthInPixels(*((char *)paramPtr->valuePtr + (sizeof(char) * editStringIdx))), 
                            paramPtr->posY + IPSDisplay_getCharHeightInPixels(), 2, screenColourWhite);
                    }
                    break;

                //Counter-clockwise event
                case encoder0_ccw:
                    //If index of currently selected string parameter
                    //is greater than zero, it can be decremented
                    if(editStringIdx > 0)
                    {
                        //erase present character selection underline
                        IPSDisplay_drawHorizontalLineToScreen(editMarkerPosXOffset, 
                            editMarkerPosXOffset + IPSDisplay_getCharWidthInPixels(*((char *)paramPtr->valuePtr + (sizeof(char) * editStringIdx))), 
                            paramPtr->posY + IPSDisplay_getCharHeightInPixels(), 2, screenColourBlack);

                        editStringIdx--; //Update the index of charcter currently being edited to new selection
                        //Update X offset for the placement of the underline to its new selection position
                        editMarkerPosXOffset -= IPSDisplay_getCharWidthInPixels(*((char *)paramPtr->valuePtr + (sizeof(char) * editStringIdx))) + 1;

                        //Draw new character selection underline
                        IPSDisplay_drawHorizontalLineToScreen(editMarkerPosXOffset, 
                            editMarkerPosXOffset + IPSDisplay_getCharWidthInPixels(*((char *)paramPtr->valuePtr + (sizeof(char) * editStringIdx))), 
                            paramPtr->posY + IPSDisplay_getCharHeightInPixels(), 2, screenColourWhite);
                    } 
                    break;

                //Switch event
                case encoder1_sw:   
                    //User has exited string edit process, so set exit flag
                    flags.exitStringEditProcess = true;
                    break;

                //Clockwise event 
                case encoder1_cw:   
                    //String will now be edited, so set flag so display is updated
                    flags.stringHasBeenModified = true;   
                    //If the character set index is within the allowed
                    //maximum then increment, otherwise wrap back to base index
                    if(characterSetIdx < (CHARACTER_SET_NUM_CHARS - 1)) characterSetIdx++;
                    else characterSetIdx = 0;
                    break;

                //Counter-clockwise event
                case encoder1_ccw:  
                    //String will now be edited, so set flag so display is updated
                    flags.stringHasBeenModified = true;
                    //If the charcter set index is within the allowed
                    //maximum then increment, otherwise wrap to max idx
                    if(characterSetIdx > 0) characterSetIdx--;
                    else characterSetIdx = (CHARACTER_SET_NUM_CHARS - 1);
                    break;
            }
        }


        if (flags.stringHasBeenModified)  //Set to true when a character in string has been changed
        {
            flags.stringHasBeenModified = false;  //Clear flag

            //Erase the current character selection underline, it will need
            //to be redrawn once the paramter string has been updated
            IPSDisplay_drawHorizontalLineToScreen(editMarkerPosXOffset, 
                editMarkerPosXOffset + IPSDisplay_getCharWidthInPixels(*((char *)paramPtr->valuePtr + (sizeof(char) * editStringIdx))), 
                paramPtr->posY + IPSDisplay_getCharHeightInPixels(), 2, screenColourBlack);

            //Get current length of string being edited
            stringLen = strlen((char*)paramPtr->valuePtr);

            //We need to temporarily store the old string so
            //it can be correctly erased from the display
            memset(stringCharArray, 0, MAX_STRING_CHARS);
            strcpy(stringCharArray, (char*)paramPtr->valuePtr);      //Copy the old string to storage

            //Now we can update the string parameter being edited 
            *((char*)paramPtr->valuePtr + sizeof(char) * editStringIdx) = characterSet[characterSetIdx];

            //ESP_LOGI(LOG_TAG, "StringLen = %d", stringLen);
            //ESP_LOGI(LOG_TAG, "stringCharArray: %s", stringCharArray);

            //Erase previous string by painting over it in background colour
            IPSDisplay_drawLineOfTextToScreen(stringCharArray, stringLen, paramPtr->posX, paramPtr->posY, screenColourBlack);

            //Get length of the new string incase its changed
            stringLen = strlen((char *)paramPtr->valuePtr);   

            //ESP_LOGI(LOG_TAG, "StringLen = %d", stringLen);
            //ESP_LOGI(LOG_TAG, "stringCharArray: %s", (char *)paramPtr->valuePtr);

            //Draw new string to display in the foreground colour
            IPSDisplay_drawLineOfTextToScreen((char *)paramPtr->valuePtr, stringLen, paramPtr->posX, paramPtr->posY, screenColourWhite);

            //Redraw the current selection underline, we do this so
            //its length matches the pixel width of the new character
            IPSDisplay_drawHorizontalLineToScreen(editMarkerPosXOffset, 
                editMarkerPosXOffset + IPSDisplay_getCharWidthInPixels(*((char *)paramPtr->valuePtr + (sizeof(char) * editStringIdx))), 
                paramPtr->posY + IPSDisplay_getCharHeightInPixels(), 2, screenColourWhite);
        }
        else if (flags.exitStringEditProcess) //Set to true when user leaves string edit mode
        {
            flags.exitStringEditProcess = false; //Clear flags

            //Need to remove the current chacacter selection
            //underline before we can exit the string edit process,
            //erase underline by painting over in background colour
            IPSDisplay_drawHorizontalLineToScreen(editMarkerPosXOffset, 
                editMarkerPosXOffset + IPSDisplay_getCharWidthInPixels(*((char *)paramPtr->valuePtr + (sizeof(char) * editStringIdx))), 
                paramPtr->posY + IPSDisplay_getCharHeightInPixels(), 2, screenColourBlack);

            break; //---- EXIT STRING EDIT PROCESS LOOP ----
        } 

        vTaskDelay(1);  //Smash the idle task to re-set watchdg
    }
}


//---- Private
static uint8_t createDefaultProjectName(void * unusedParam)
{
    //This callback function is called when the user begins a new project,
    //it generates a temporary default project name and assigns that
    //name string to the appropriate paramater pointer.

    uint8_t uniqueID = 0;
    uint16_t idx = 0;
    param_t *paramPtr = NULL;
    bool doneFlag = true;

    //Clear out any previous data that may be lurking
    memset(g_projectNameArr, 0, MAX_PROJECT_NAME_LENGTH);

    //The project name menu item is always the
    //base idx of the 'start_new_project', so find that base idx
    while (menuManagerPtr[idx].menuPageCode != state_new_project)
    {
        //TODO: Add getout (define MAX idx once system defined)
        idx++;
    }

    //Abort if the param for that idx isn't a string
    if (menuManagerPtr[idx].paramType != param_string) return 1;

    while(1)
    {
        doneFlag = true;
        snprintf(g_projectNameArr, MAX_PROJECT_NAME_LENGTH, "Proj%d", uniqueID);

        for(uint8_t a = 0; a < *g_numFileNamesPtr; ++a)
        {
            if(strcmp(g_projectNameArr, &g_fileNamesPtr[a][0]) == 0) //if already exists
            {
                ++uniqueID;
                doneFlag = false;
                break;
            }
        }

        if(doneFlag == true || uniqueID > 200) break;
    }

    //Grab direct pointer to the parameter
    paramPtr = (param_t *)menuManagerPtr[idx].paramPtr;

    if(paramPtr == NULL) return 1;

    if(uniqueID > 200)
    {
        paramPtr->valuePtr = NULL;
        return 1;
    } 
    else
    {
        //ESP_LOGI(LOG_TAG, "Generated new project name: '%s'", g_projectNameArr);
        paramPtr->valuePtr = g_projectNameArr;
        return 0;
    }
}


//---- Private
void moveSelectionIndicator(bool isUpOrDown)
{
    //This function is used to erase and redraw the menus current 
    //selection graphical indicator as the used scrolls through
    //menu items on the current menu page.

    uint16_t oldX = g_MenuData.selectionIndicator.xStartPos;
    uint16_t oldY = g_MenuData.selectionIndicator.yPosCurrent;

    //Move selector UP if TRUE
    //Move selector DOWN if FLASE
    if (isUpOrDown)
    {
        if (g_MenuData.selectionIndicator.currentItem > 0)
        {
            g_MenuData.selectionIndicator.yPosCurrent -= MENU_LINE_MOVE_BY;
            g_MenuData.selectionIndicator.currentItem--;
        }
        else return;
    }
    else
    {
        if (g_MenuData.selectionIndicator.currentItem < (g_MenuData.selectableItems - 1))
        {
            g_MenuData.selectionIndicator.yPosCurrent += MENU_LINE_MOVE_BY;
            g_MenuData.selectionIndicator.currentItem++;
        }
        else return;
    }

    //Remove menu current selection indicator pixels from previous location
    IPSDisplay_drawLineOfTextToScreen(MENU_SELECTOR_CHAR, 1, oldX, oldY, screenColourBlack); //delete previous
    //Draw new menu current selection indicator pixels at updated location
    IPSDisplay_drawLineOfTextToScreen(MENU_SELECTOR_CHAR, 1, g_MenuData.selectionIndicator.xStartPos, g_MenuData.selectionIndicator.yPosCurrent, screenColourWhite); //draw new
}


//---- Private
void resetMenuIndicator(void)
{
    //This function is used to reset the menus current
    //selection graphical indicator whenever a new menu page is opened 
    IPSDisplay_drawLineOfTextToScreen(MENU_SELECTOR_CHAR, 1, g_MenuData.selectionIndicator.xStartPos, MENU_ITEM_START_Y, screenColourWhite);
    g_MenuData.selectionIndicator.yPosCurrent = MENU_ITEM_START_Y;
    g_MenuData.selectionIndicator.currentItem = 0;
}




//MENU CALLBACKS - ONLY FOR TESTING ATM

uint8_t setProjectTempoCallback(void * param)
{
    if(param != NULL)
    {
        g_MenuEventReturn.eventOpcode = 5;
        g_MenuEventReturn.eventData = param;
    }
    return 0;
}

uint8_t setProjectQuantizationCallback(void * param)
{
    static uint8_t quantization;

    if(param != NULL)
    {
        g_MenuEventReturn.eventOpcode = 7;

        if(strcmp((char*)param, "1/1 Note") == 0)
        {
            quantization = 1;
        }
        else if(strcmp((char*)param, "1/2 Note") == 0)
        {
            quantization = 2;
        }
        else if(strcmp((char*)param, "1/4 Note") == 0)
        {
            quantization = 4;
        }
        else if(strcmp((char*)param, "1/8 Note") == 0)
        {
            quantization = 8;
        }
        else if(strcmp((char*)param, "1/16 Note") == 0)
        {
            quantization = 16;
        }
        
        g_MenuEventReturn.eventData = &quantization;
    }
    return 0;
}

uint8_t setProjectNameCallback(void * param)
{
    if(param != NULL)
    {
        g_MenuEventReturn.eventOpcode = 6;
        g_MenuEventReturn.eventData = param;
    }
    return 0;
}

uint8_t createNewProjectFileCallback(void * param)
{
    g_MenuEventReturn.eventOpcode = 1;
    g_MenuEventReturn.eventData = NULL;
    return 0;
}