

typedef struct {
    uint8_t eventOpcode;
    void * eventData;
} MenuQueueItem;

//Public Interface
//void guiMenu_init(const char * fileNamesPtr[], const uint8_t * const numFilesOnSystem);

extern QueueHandle_t g_MenuToSystemQueueHandle;

void guiMenu_entryPoint(void * params);