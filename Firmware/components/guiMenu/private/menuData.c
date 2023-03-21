#include "menuData.h"


//THESE WILL BE PLACED INTO FLASH
static char dispTxt_newProj[]     = "NEW PROJECT";
static char dispTxt_loadProj[]    = "LOAD PROJECT";
static char dispTxt_system[]      = "SYSTEM CONFIG";
static char dispTxt_disk[]        = "DISK OPERATIONS";
//
static char dispTxt_projName[]    = "NAME: ";
static char dispTxt_projTempo[]   = "TEMPO: ";
static char dispTxt_projQuant[]   = "QUANT: ";
static char dispTxt_projCreate[]  = "CONTINUE.. ";
//
static char dispTxt_noteNum[]      = "NOTE: ";
static char dispTxt_noteVelocity[] = "VELOCITY: ";
static char dispTxt_noteDuration[] = "DURATION: ";

static char * dispTxt_quant[5]    = {"1/1 Note", "1/2 Note", "1/4 Note", "1/8 Note", "1/16 Note" };


uint8_t initTempo;
uint8_t noteNum;
uint8_t noteVelocity;
uint8_t noteDuration;

MenuParamSelection paramQuant = {dispTxt_quant, 5, 0, 0, 0};

MenuParam paramTempo = {&initTempo, 60, 240, 0, 0};
MenuParam projName = {NULL, 0,0,0,0};
MenuParam paramNoteNum = {&noteNum, 0, 0, 0, 0};
MenuParam paramNoteVelocity = {&noteVelocity, 0, 0, 0, 0};
MenuParam paramNoteDuration = {&noteDuration, 0, 0, 0, 0};

menuData_t menuDataArr[] =
{

    //-------------------------------------------------
    //------ MENU BASE PAGE (LOADED AT STARTUP) -------
    //-------------------------------------------------
    { 
      state_base,           //Opcode for *this* menu page
      dispTxt_newProj,      //Pointer to line txt
      param_none,           //Specify the parameter type
      NULL,                 //Pointer to assosiated parameter, if one exists
      0,                    //MENU state to transition to if menu 'back' is pressed  - SET TO ZERO IF NO CHANGE
      state_new_project,    //MENU state to transition to if menu 'enter' is pressed - SET TO ZERO IF NO CHANGE
      NULL                  //Optional Function to execute on select button press
    },

    { 
      state_base,
      dispTxt_loadProj,
      param_none,
      NULL,
      0,
      0,
      NULL
    },

    { 
      state_base,
      dispTxt_system,
      param_none,
      NULL,
      0,
      0,
      NULL
    },

    { 
      state_base,
      dispTxt_disk,
      param_none,
      NULL,
      0,
      0,
      NULL
    },

    //-------------------------------------------------
    //--------- NEW PROJECT MENU LAYER ENTRIES --------
    //-------------------------------------------------
    { 
      state_new_project,
      dispTxt_projName,
      param_string,
      &projName,
      state_base,
      0,
      NULL
    },

    { 
      state_new_project,
      dispTxt_projTempo,
      param_numeric,
      &paramTempo,
      state_base,
      0,
      NULL
    },

    { 
      state_new_project,
      dispTxt_projQuant,
      param_string_selection,
      &paramQuant,
      state_base,
      0,
      NULL
    },

    { 
      state_new_project,
      dispTxt_projCreate,
      param_none,
      NULL,
      state_base,
      0,
      createNewProjectFileCallback
    },

    //-------------------------------------------------
    //------------ GRID EDIT MODE PAGE ----------------
    //-------------------------------------------------

    {
      state_grid_edit, //ADD NOTES
      NULL,
      param_none,
      NULL,
      state_base,
      0,
      NULL
    },

    {
      state_grid_edit, //REMOVE NOTES
      NULL,
      param_none,
      NULL,
      state_base,
      0,
      NULL
    },

    {
      state_grid_edit, //PLAYBACK PROJECT
      NULL,
      param_none,
      NULL,
      state_base,
      0,
      NULL
    },

    {
      state_grid_edit, //SAVE PROJECT
      NULL,
      param_none,
      NULL,
      state_base,
      0,
      NULL
    },

    {
      state_grid_edit, //CLOSE PROJECT
      NULL,
      param_none,
      NULL,
      state_base,
      0,
      NULL
    },

    //-------------------------------------------------
    //------------ NOTE EVENT ENTRY PAGE  -------------
    //-------------------------------------------------

    { 
      state_note_edit,
      dispTxt_noteNum,
      param_numeric,
      &paramNoteNum,
      state_base,
      0,
      NULL
    },

    { 
      state_note_edit,
      dispTxt_noteVelocity,
      param_numeric,
      &paramNoteVelocity,
      state_base,
      0,
      updateNoteVelocity
    },

    { 
      state_note_edit,
      dispTxt_noteDuration,
      param_numeric,
      &paramNoteDuration,
      state_base,
      0,
      updateNoteDuration
    },


    {endOfPages,NULL,param_none,NULL,0,0,NULL}   //MARKS END OF ARRAY

};

menuData_t * const menuManagerPtr = menuDataArr;