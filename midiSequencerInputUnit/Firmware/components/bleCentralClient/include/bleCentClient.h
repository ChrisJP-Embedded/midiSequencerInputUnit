

<<<<<<< HEAD
enum 
{
=======



enum {
>>>>>>> 2e307e5 (Started to plumb in bt central (gattServer) using Ning NimBLE stack - ongoing)
    longWriteToPeripheral,
    writeToPeripheral,
    readFromPeripheral,
    shutdownBle,
    stopPlayback,
    startPlayback
};

<<<<<<< HEAD
//Use this for ALL queue items sent from app to bt
//if we want to do a long data transfer the data section
//will hold a pointer to psram allocated byte array
typedef struct {
    uint8_t opcode;
    uint32_t dataLength;
    uint8_t * dataPtr;
} HostToBleQueueItem_t;
=======
typedef struct 
{
    uint8_t opcode;
    uint8_t * data;
    uint32_t len;
} appToBleQueueItem_t;
>>>>>>> 2e307e5 (Started to plumb in bt central (gattServer) using Ning NimBLE stack - ongoing)


void bleCentAPI_task(void * param);

<<<<<<< HEAD
extern volatile bool isConnectedToTargetDevice;

extern QueueHandle_t g_HostToBleQueueHandle;
extern QueueHandle_t g_BleToHostQueueHandle;
=======
extern QueueHandle_t appToBleQueue;
extern QueueHandle_t bleToAppQueue;
>>>>>>> 2e307e5 (Started to plumb in bt central (gattServer) using Ning NimBLE stack - ongoing)
