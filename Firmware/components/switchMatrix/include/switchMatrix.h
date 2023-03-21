
#define SWITCH_MATRIX_QUEUE_NUM_ITEMS 1

typedef struct {
    uint16_t column;
    uint16_t row;
} SwitchMatrixQueueItem;

extern QueueHandle_t g_SwitchMatrixQueueHandle;
void switchMatrix_TaskEntryPoint(void * taskParams);