

#define MENU_QUEUE_ITEM_PAYLOAD_SIZE 10

typedef struct {
    uint8_t eventOpcode;
    uint8_t payload[MENU_QUEUE_ITEM_PAYLOAD_SIZE];
} MenuQueueItem;

//Public Interface
//void guiMenu_init(const char * fileNamesPtr[], const uint8_t * const numFilesOnSystem);

extern QueueHandle_t g_MenuToSystemQueueHandle;
extern QueueHandle_t g_SystemToMenuQueueHandle;

void guiMenu_entryPoint(void * params);