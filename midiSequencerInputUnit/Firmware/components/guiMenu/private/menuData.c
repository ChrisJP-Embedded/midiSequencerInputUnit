#include "menuData.h"


//THESE WILL BE PLACED INTO FLASH
static char dispTxt_newProj[]     = "NEW PROJECT";
static char dispTxt_loadProj[]    = "LOAD PROJECT";
static char dispTxt_system[]      = "SYSTEM CONFIG";
static char dispTxt_disk[]        = "DISK OPERATIONS";

static char dispTxt_projName[]    = "NAME: ";
static char dispTxt_projTempo[]   = "TEMPO: ";
static char dispTxt_projQuant[]   = "QUANT: ";
static char dispTxt_projCreate[]  = "SAVE & CONTINUE: ";

static char * dispTxt_quant[5]    = {"1/1 Note", "1/2 Note", "1/4 Note", "1/8 Note", "1/16 Note" };

static char numberArray[5] = {1,2,3,4,5};

uint8_t initTempo;

param_t paramTempo = {&initTempo, 60, 240, 0, 0};
param_t projName = {NULL, 0,0,0,0};

param_selection_t paramQuant = {dispTxt_quant, 5, 0, 0};
param_selection_t nums = {&numberArray, 5, 0, 0};


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
      NULL                  //Optional Function to execute
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
      setProjectNameCallback
    },

    { 
      state_new_project,
      dispTxt_projTempo,
      param_numeric,
      &paramTempo,
      state_base,
      0,
      setProjectTempoCallback
    },

    { 
      state_new_project,
      dispTxt_projQuant,
      param_string_selection,
      &paramQuant,
      state_base,
      0,
      setProjectQuantizationCallback
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
    //----------- NOTE EVEVENT ENTRY PAGE  ------------
    //-------------------------------------------------
    { 
      state_note_entry,
      dispTxt_projCreate,
      param_none,
      NULL,
      state_base,
      0,
      createNewProjectFileCallback
    },


    {endOfPages,NULL,param_none,NULL,0,0,NULL}   //MARKS END OF ARRAY

};

menuData_t * const menuManagerPtr = menuDataArr;