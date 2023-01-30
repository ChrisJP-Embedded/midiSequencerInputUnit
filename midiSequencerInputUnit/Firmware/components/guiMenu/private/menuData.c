#include "menuData.h"


//THESE WILL BE PLACED INTO FLASH
static char dispTxt_newProj[]        = "NEW PROJECT";
static char dispTxt_loadProj[]       = "LOAD PROJECT";
static char dispTxt_system[]         = "SYSTEM CONFIG";
static char dispTxt_disk[]           = "DISK OPERATIONS";

static char dispTxt_projName[]       = "NAME: ";
static char dispTxt_projTempo[]      = "TEMPO: ";
static char dispTxt_projQuant[]      = "QUANT: ";
static char dispTxt_projCreate[]     = "SAVE & CONTINUE: ";

uint8_t initTempo = 120;

static char dispTxt_quant0[] = "1/1 Note";
static char dispTxt_quant1[] = "1/2 Note";
static char dispTxt_quant2[] = "1/4 Note";
static char dispTxt_quant3[] = "1/8 Note";
static char dispTxt_quant4[] = "1/16 Note";





static char * initQuant[5] = {dispTxt_quant0, dispTxt_quant1, dispTxt_quant2, dispTxt_quant3, dispTxt_quant4};
//******** example of numeric selection ********//
//static uint8_t numericSelection[] = {5, 20, 45, 2, 1};
//param_selection_t paramTempo = {numericSelection, &numericSelection[4], &numericSelection[0], 0, 0};


param_t paramTempo = {&initTempo, 240, 40, 0, 0};
param_t projName = {NULL, 0,0,0,0};


param_selection_t paramQuant = {initQuant, &initQuant[4], &initQuant[0], 0, 0};


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