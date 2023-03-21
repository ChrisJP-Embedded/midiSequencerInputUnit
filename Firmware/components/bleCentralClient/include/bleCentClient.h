
enum 
{
    longWriteToPeripheral,
    writeToPeripheral,
    readFromPeripheral,
    shutdownBle,
    stopPlayback,
    startPlayback
};

//Use this for ALL queue items sent from app to bt
//if we want to do a long data transfer the data section
//will hold a pointer to psram allocated byte array
typedef struct {
    uint8_t opcode;
    uint32_t dataLength;
    uint8_t * dataPtr;
} HostToBleQueueItem;

void bleCentAPI_task(void * param);

extern volatile bool isConnectedToTargetDevice;

extern QueueHandle_t g_HostToBleQueueHandle;
extern QueueHandle_t g_BleToHostQueueHandle;

