


typedef struct {
    uint16_t column;
    uint16_t row;
} SwitchMatrixQueueItem_t;

extern QueueHandle_t g_SwitchMatrixQueueHandle;
void switchMatrix_TaskEntryPoint(void * taskParams);