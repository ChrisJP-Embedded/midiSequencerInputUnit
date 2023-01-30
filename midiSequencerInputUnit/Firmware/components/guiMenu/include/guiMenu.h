


typedef struct {
    uint8_t eventOpcode;
    void * eventData;
} MenuEventData_t;

//Public Interface
void guiMenu_init(const char * fileNamesPtr[], const uint8_t * const numFilesOnSystem);
MenuEventData_t guiMenu_interface(uint8_t menuInputBuffer);