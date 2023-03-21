#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "include/bleCentClient.h"
#include "bleCentPrivate.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"


#define LOG_TAG "bleGattClient"
#define SUCCESS 0
#define FAIL    1
#define CONNECTION_MAX_RETRIES 10

static char * addr_str(const void *addr);
static void blecent_scan(void);
static uint8_t initNimBle(void);
static void blecent_on_sync(void);
static void blecent_on_reset(int reason);
static int gapEventHander(struct ble_gap_event *event, void *arg);
static void connectIfTargetFound(const struct ble_gap_disc_desc *disc);
static void discoveryProcessComplete(const struct peer *peer, int status, void *arg);

void ble_store_config_init(void);


//********* Target service UUID - The central will scan for advertisements, if any found 
//********* advertisement includes the target service uuid a connection request will be made
static const ble_uuid128_t targetServiceUUID128 = BLE_UUID128_INIT(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12, 0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);

//********* Target characteristic UUID
static const ble_uuid128_t char0_uuid128 = BLE_UUID128_INIT(0xf6, 0x6d, 0xc9, 0x07, 0x71, 0x00, 0x16, 0xb0, 0xe1, 0x45, 0x7e, 0x89, 0x9e, 0x65, 0x3a, 0x5c);

//********* Target characteristic UUID
static const ble_uuid128_t char1_uuid128 = BLE_UUID128_INIT(0xf7, 0x6d, 0xc9, 0x07, 0x71, 0x00, 0x16, 0xb0, 0xe1, 0x45, 0x7e, 0x89, 0x9e, 0x65, 0x3a, 0x5c);

volatile uint16_t connectionHandle;
const struct peer_chr * characteristic_0 = NULL;
const struct peer_chr * characteristic_1 = NULL;

QueueHandle_t g_HostToBleQueueHandle;
QueueHandle_t g_BleToHostQueueHandle;

volatile bool isConnectedToTargetDevice = false;
volatile bool Var = false;


//*********************************
//This is the BLE central RTOS task
//*********************************
void blecent_host_task(void *param)
{
    ESP_LOGI(LOG_TAG, "BLE Host Task Started");
    nimble_port_run(); //This function will return only when nimble_port_stop() is executed
    nimble_port_freertos_deinit();
}

typedef struct{
    uint8_t flags;
    uint8_t opcode;
    uint8_t data[510];
}bleTXwrapper_t;


int bleDoneISR(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    Var = true;
    return 0;
}

//*************************************
//This is the BLE runtime API RTOS task
//used to communicate with BLE Central
//*************************************
void bleCentAPI_task(void * param)
{
    uint8_t state = 0;
    uint8_t * localDataPtr = NULL;
    uint32_t localDataLength = 0;

    uint32_t numBytesSent = 0;
    bleTXwrapper_t bleTXpayload;


    //Used for ALL app to BLE comms
    HostToBleQueueItem g_HostToBleQueueHandleItem;

    uint8_t responseForApp;

    //SEND QUEUE ITEM TO APP
    //TO INDICATE BLE IS READY
    //TO RECEIEVE COMMANDS 
    initNimBle();

    //Just a dummy packet to indicate that the task started successfully
    if(xQueueSend(g_BleToHostQueueHandle, &responseForApp, pdMS_TO_TICKS(5000)) == pdFALSE)
    {
        ESP_LOGE(LOG_TAG, "Failure adding item to g_BleToHostQueueHandle - ble task startup failed, deleting task");
        vTaskDelete(NULL); //Delete *this* task
    }

    while(1)
    {

        if(uxQueueMessagesWaiting(g_HostToBleQueueHandle))
        {
            //Receieve from queue - dont wait for data to become available
            if(xQueueReceive(g_HostToBleQueueHandle, &g_HostToBleQueueHandleItem, 0) == 1)
            {
                ESP_LOGI(LOG_TAG, "New queue item recieved from system level");

                if(g_HostToBleQueueHandleItem.opcode == 0x55)
                {
                    ESP_LOGI(LOG_TAG, "File playback requested");
                    state = 1;
                }
            }
        }


        switch(state)
        {
            case 0:
                break;

            case 1: //Starting new playback stream
                ESP_LOGI(LOG_TAG, "Playback first packet (TOTAL bytes: %ld", g_HostToBleQueueHandleItem.dataLength);
                bleTXpayload.flags = 0b00100000;
                bleTXpayload.opcode = 0x01;
                localDataPtr = g_HostToBleQueueHandleItem.dataPtr;
                localDataLength = g_HostToBleQueueHandleItem.dataLength;
                memcpy(bleTXpayload.data, localDataPtr, 510);
                Var = false;
                ble_gattc_write_flat(connectionHandle, characteristic_0->chr.val_handle, &bleTXpayload, 512, bleDoneISR, NULL);
                numBytesSent = 510;
                state++;
                break;

            case 2: //Ongoing playback stream
                if(Var == true)
                {
                    ESP_LOGI(LOG_TAG, "Ongoing packet..");
                    bleTXpayload.flags = 0b00010000;
                    bleTXpayload.opcode = 0x02;
                    memcpy(bleTXpayload.data, localDataPtr + numBytesSent, 510);
                    Var = false;
                    ble_gattc_write_flat(connectionHandle, characteristic_0->chr.val_handle, &bleTXpayload, 512, bleDoneISR, NULL);
                    numBytesSent += 510;
                    if(numBytesSent >= localDataLength)
                    {
                        ESP_LOGI(LOG_TAG, "final num bytes sent: %ld", numBytesSent);
                        state++;
                    }
                }
                break;

            case 3: 
                ESP_LOGI(LOG_TAG, "Finished playback stream");
                state = 0;
                break;

            default:
                break;
        }


        vTaskDelay(1);
    }

    nimble_port_freertos_deinit();

    responseForApp = 0x00;  //some number or enum to say connection failed
    if(xQueueSendToBack(g_BleToHostQueueHandle, (void*)&responseForApp, pdMS_TO_TICKS(1000)) == pdFALSE)

    {
        ESP_LOGE(LOG_TAG, "Failed to add item to g_BleToHostQueueHandle, ble task deleted");
    }

    //nvs_flash_deinit();
    vTaskDelete(NULL); //Delete *this* task
}





static uint8_t initNimBle(void)
{
    int retVal;

    //Initialize NVS — it is used to store PHY calibration data
    esp_err_t ret = nvs_flash_init();
    if  (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nimble_port_init();

    //Configure the host.
    ble_hs_cfg.reset_cb = blecent_on_reset;
    ble_hs_cfg.sync_cb = blecent_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    //Initialize data structures to track connected peers
    retVal = peer_init(MYNEWT_VAL(BLE_MAX_CONNECTIONS), 64, 64, 64);
    assert(retVal == 0);

    //Set the default device name.
    retVal = ble_svc_gap_device_name_set("nimble-blecent");
    assert(retVal == 0);

    //XXX Need to have template for store
    ble_store_config_init();

    nimble_port_freertos_init(blecent_host_task);

    return 0;
}


static void discoveryProcessComplete(const struct peer *peer, int status, void *arg)
{
    //A connection has been made and the discovery process has finished,
    //we now have a complete list of services, characteristics and
    //descriptors that the peer supports.

    if (status != 0) {
        /* Service discovery failed.  Terminate the connection. */
        MODLOG_DFLT(ERROR, "Error: Service discovery failed; status=%d " "conn_handle=%d\n", status, peer->conn_handle);
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    MODLOG_DFLT(INFO, "Service discovery complete; status=%d " "conn_handle=%d\n", status, peer->conn_handle);



    //Global structs which let us reference characteristics globally
    characteristic_0 = peer_chr_find_uuid(peer, (ble_uuid_t*)&targetServiceUUID128.u, (ble_uuid_t*)&char0_uuid128);
    if (characteristic_0 == NULL) {
        MODLOG_DFLT(ERROR, "Error: One of target characteristics not found");
        goto error;
    }


    characteristic_1 = peer_chr_find_uuid(peer, (ble_uuid_t*)&targetServiceUUID128.u, (ble_uuid_t*)&char1_uuid128);
    if (characteristic_1 == NULL) {
        MODLOG_DFLT(ERROR, "Error: One of target characteristics not found");
        goto error;
    }

    ESP_LOGI(LOG_TAG, "All target characteristics found!");

    connectionHandle = peer->conn_handle;

    isConnectedToTargetDevice = true;

    return;
    
    error:
    ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

static void blecent_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params;
    int rc;

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Tell the controller to filter duplicates; we don't want to process
     * repeated advertisements from the same device.
     */
    disc_params.filter_duplicates = 1;

    /**
     * Perform a passive scan.  I.e., don't send follow-up scan requests to
     * each advertiser.
     */
    disc_params.passive = 1;

    /* Use defaults for the rest of the parameters. */
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, gapEventHander, NULL);

    if (rc != 0) {
        MODLOG_DFLT(ERROR, "Error initiating GAP discovery procedure; rc=%d\n",
                    rc);
    }
}

 //Connects to the sender of the specified advertisement of it looks
 //interesting.  A device is "interesting" if it advertises connectability 
 //and support for the Alert Notification service.
static void connectIfTargetFound(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields; //This struct will hold the data contained in advertisement fields (which is configured on the peripheral side)
    uint8_t own_addr_type;
    int retVal;

    //If the device is not advertising connectability then ignore
    if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND && disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) return;

    //Copy across receieved advertisement fields into local cache
    retVal = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (retVal != 0) return;


    ble_uuid_t* target_peripheral_uuid = NULL;
    ble_uuid_t* peripheral_uuid_in_advertisement = (ble_uuid_t*) &targetServiceUUID128;
    //***** IMPORTANT
    //The target device must place the the 128Bit UUID
    //of the target service in its advertisement fields
    //If the target service UUID is found, a connection is made
    for (int i = 0; i < fields.num_uuids128; i++) 
    {

        target_peripheral_uuid = (ble_uuid_t*) &fields.uuids128[i].u;

        if (ble_uuid_cmp(target_peripheral_uuid, peripheral_uuid_in_advertisement) == 0)
        {
            retVal=1;
            break;
        } 
    }

    if(retVal == 0) return; //Target service UUID not found - abort!


    //------------------------------------------------------------
    //---- To reach here we must have found the target device ----
    //------------------------------------------------------------

    //Scanning must be stopped before a connection can be initiated
    retVal = ble_gap_disc_cancel();
    if (retVal != 0) 
    {
        MODLOG_DFLT(DEBUG, "Failed to cancel scan; retVal=%d\n", retVal);
        return;
    }

    //Figure out address to use for connect (no privacy for now) 
    retVal = ble_hs_id_infer_auto(0, &own_addr_type);
    if (retVal != 0) 
    {
        MODLOG_DFLT(ERROR, "error determining address type; retVal=%d\n", retVal);
        return;
    }

    //Try to connect the the advertiser. Allow 30 seconds (30000 ms) for timeout.
    retVal = ble_gap_connect(own_addr_type, &disc->addr, 30000, NULL, gapEventHander, NULL);

    if (retVal != 0) 
    {
        MODLOG_DFLT(ERROR, "Error: Failed to connect to device; addr_type=%d "
                    "addr=%s; retVal=%d\n",
                    disc->addr.type, addr_str(disc->addr.val), retVal);
        return;
    }
}


/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that is
 * established.  blecent uses the same callback for all connections.
 *
 * @param event                 The event being signalled.
 * @param arg                   Application-specified argument; unused by
 *                                  blecent.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int gapEventHander(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_hs_adv_fields fields;
    int retVal;

    switch (event->type) 
    {
        case BLE_GAP_EVENT_DISC:
            retVal = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
            if (retVal != 0) return 0;
            //print_adv_fields(&fields);
            connectIfTargetFound(&event->disc);
            return 0;

        case BLE_GAP_EVENT_CONNECT:
            /* A new connection was established or a connection attempt failed. */
            if (event->connect.status == 0) 
            {
                //--------------------------------
                //---- SUCCESSFULLY CONNECTED ----
                //--------------------------------
                
                MODLOG_DFLT(INFO, "Connection established ");

                retVal = ble_gap_conn_find(event->connect.conn_handle, &desc);
                assert(retVal == 0);
                //print_conn_desc(&desc);
                MODLOG_DFLT(INFO, "\n");

                //Remember peer
                retVal = peer_add(event->connect.conn_handle);
                if (retVal != 0) 
                {
                    MODLOG_DFLT(ERROR, "Failed to add peer; retVal=%d\n", retVal);
                    return 0;
                }

                //Discover all serverices on the peripheral
                retVal = peer_disc_all(event->connect.conn_handle, discoveryProcessComplete, NULL);

                if (retVal != 0) 
                {
                    MODLOG_DFLT(ERROR, "Failed to discover services; retVal=%d\n", retVal);
                    return 0;
                }

                //IMPORTANT
                //Increase default mtu (23)
                //to maximum allowed by esp32 (517)
                ble_att_set_preferred_mtu(517);
                ble_gattc_exchange_mtu(event->connect.conn_handle, NULL, NULL);
            } 
            else 
            {
                //Connection attempt failed; resume scanning
                MODLOG_DFLT(ERROR, "Error: Connection failed; status=%d\n", event->connect.status);
                blecent_scan();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            /* Connection terminated. */
            MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
            //print_conn_desc(&event->disconnect.conn);
            MODLOG_DFLT(INFO, "\n");

            peer_delete(event->disconnect.conn.conn_handle);
            blecent_scan();
            return 0;


        case BLE_GAP_EVENT_DISC_COMPLETE:
            return 0;


        case BLE_GAP_EVENT_ENC_CHANGE:
            return 0;


        case BLE_GAP_EVENT_NOTIFY_RX:
            /* Peer sent us a notification or indication. */
            MODLOG_DFLT(INFO, "received %s; conn_handle=%d attr_handle=%d "
                        "attr_len=%d\n",
                        event->notify_rx.indication ?
                        "indication" :
                        "notification",
                        event->notify_rx.conn_handle,
                        event->notify_rx.attr_handle,
                        OS_MBUF_PKTLEN(event->notify_rx.om));

            /* Attribute data is contained in event->notify_rx.om. Use
            * `os_mbuf_copydata` to copy the data received in notification mbuf */
            return 0;

        case BLE_GAP_EVENT_MTU:
            MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                        event->mtu.conn_handle,
                        event->mtu.channel_id,
                        event->mtu.value);
            return 0;

        case BLE_GAP_EVENT_REPEAT_PAIRING:
            /* We already have a bond with the peer, but it is attempting to
            * establish a new secure link.  This app sacrifices security for
            * convenience: just throw away the old bond and accept the new link.
            */

            /* Delete the old bond. */
            retVal = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            assert(retVal == 0);
            ble_store_util_delete_peer(&desc.peer_id_addr);

            /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
            * continue with the pairing operation.
            */
            return BLE_GAP_REPEAT_PAIRING_RETRY;

        default:
            return 0;
    }
}


static void blecent_on_sync(void)
{
    //CALLBACK - Executed after host and bt controller
    //have synced and the bt operations may begin

    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params;
    int retVal;

    /* Make sure we have proper identity address set (public preferred) */
    retVal = ble_hs_util_ensure_addr(0);
    assert(retVal == 0); //Error handling (update)

    //----------------------------------------------------
    //---- START SCANNING FOR ADVERTISING PERIPHERALS ----
    //----------------------------------------------------


    /* Figure out address to use while advertising (no privacy for now) */
    retVal = ble_hs_id_infer_auto(0, &own_addr_type);
    if (retVal != 0) {
        MODLOG_DFLT(ERROR, "error determining address type; retVal=%d\n", retVal);
        return;
    }

    //Tell the controller to filter duplicates; we don't want to process
    //repeated advertisements from the same device.
    disc_params.filter_duplicates = 1;

    //Perform a passive scan.  I.e., don't send 
    //follow-up scan requests to each advertiser.
    disc_params.passive = 1;

    /* Use defaults for the rest of the parameters. */
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    //**** Initialise discovery of advertising peripheral devices (gatt servers)
    //Sets the GAP EVENT CALLBACK to 'gapEventHander'
    retVal = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, gapEventHander, NULL);

    if (retVal != 0) 
    {
        MODLOG_DFLT(ERROR, "Error initiating GAP discovery procedure; retVal=%d\n", retVal);
    }
}


static void blecent_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

static char * addr_str(const void *addr)
{
    static char buf[6 * 2 + 5 + 1];
    const uint8_t *u8p;

    u8p = addr;
    sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x",
            u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);

    return buf;
}