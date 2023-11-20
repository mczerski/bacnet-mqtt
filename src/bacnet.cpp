#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include "bacnet.hpp"

/* buffer used for receive */
static uint8_t Rx_Buf[MAX_MPDU] = { 0 };

/* debug info printing */
static bool BACnet_Debug_Enabled;

/* timers */
static struct mstimer apdu_timer = { 0 }; 
static struct mstimer datalink_timer = { 0 };

#define BAC_ADDRESS_MULT 1

struct device_entry {
    std::string name;
    std::vector<BACNET_OBJECT_ID> object_list;
};
std::unordered_map<uint32_t, device_entry> device_map;

static void device_map_add(uint32_t device_id)
{
    if (device_map.count(device_id))
        return;

    if (BACnet_Debug_Enabled) {
        fprintf(stderr, "Reading object list from device %u\n", device_id);
    }
    auto invoke_id = Send_Read_Property_Request(
        device_id,
        OBJECT_DEVICE,
        device_id,
        PROP_OBJECT_LIST,
        BACNET_ARRAY_ALL
    );
    if (invoke_id == 0) {
        fprintf(stderr, "Failed to read object list from device %u\n", device_id);
        return;
    }
    return;
}

static void i_am_handler(uint8_t *service_request, uint16_t service_len, BACNET_ADDRESS *src)
{
    handler_i_am_add(service_request, service_len, src);
    uint32_t device_id;
    bool found = address_get_device_id(src, &device_id);
    if (found) {
        device_map_add(device_id);
    }
    return;
}

static void MyAbortHandler(
    BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t abort_reason, bool server)
{
    /* FIXME: verify src and invoke id */
    (void)src;
    (void)invoke_id;
    (void)server;
    fprintf(
        stderr, "BACnet Abort: %s\n", bactext_abort_reason_name(abort_reason));
}

static void MyRejectHandler(
    BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t reject_reason)
{
    /* FIXME: verify src and invoke id */
    (void)src;
    (void)invoke_id;
    fprintf(stderr, "BACnet Reject: %s\n",
        bactext_reject_reason_name(reject_reason));
}

static void handle_object_list(uint32_t device_id, const BACNET_READ_PROPERTY_DATA& data) {
    if (BACnet_Debug_Enabled) {
        fprintf(stderr, "Received object list from device %d\n", device_id);
    }
    std::vector<BACNET_OBJECT_ID> object_list;
    uint8_t *application_data = data.application_data;
    int application_data_len = data.application_data_len;
    for (;;) {
        BACNET_APPLICATION_DATA_VALUE value;
        int len = bacapp_decode_known_property(
            application_data,
            application_data_len,
            &value,
            data.object_type,
            data.object_property
        );

        if (len < 0) {
            fprintf(stderr, "RP Ack: unable to decode! %s:%s\n",
                bactext_object_type_name(data.object_type),
                bactext_property_name(data.object_property)
            );
            break;
        }
        object_list.push_back(value.type.Object_Id);

        if (len < application_data_len) {
            application_data += len;
            application_data_len -= len;
        } else {
            device_map[device_id] = {.object_list = std::move(object_list)};
            if (BACnet_Debug_Enabled) {
                fprintf(stderr, "Adding new device entry for device %d\n", device_id);
            }
            if (BACnet_Debug_Enabled) {
                fprintf(stderr, "Reading device name from device %u\n", device_id);
            }
            int invoke_id = Send_Read_Property_Request(
                device_id,
                OBJECT_DEVICE,
                device_id,
                PROP_OBJECT_NAME,
                BACNET_ARRAY_ALL
            );
            if (invoke_id == 0) {
                fprintf(stderr, "Failed to read device name from device %u\n", device_id);
                return;
            }
            break;
        }
    }
}

static void handle_device_name(uint32_t device_id, const BACNET_READ_PROPERTY_DATA& data) {
    if (BACnet_Debug_Enabled) {
        fprintf(stderr, "Received device name from device %d\n", device_id);
    }
    BACNET_APPLICATION_DATA_VALUE value;
    int len = bacapp_decode_known_property(
        data.application_data,
        data.application_data_len,
        &value,
        data.object_type,
        data.object_property
    );

    if (len < 0) {
        fprintf(stderr, "RP Ack: unable to decode! %s:%s\n",
            bactext_object_type_name(data.object_type),
            bactext_property_name(data.object_property)
        );
        return;
    }
    device_map[device_id].name = characterstring_value(&value.type.Character_String);
}
/** Handler for a ReadProperty ACK.
 * @ingroup DSRP
 * Doesn't actually do anything, except, for debugging, to
 * print out the ACK data of a matching request.
 *
 * @param service_request [in] The contents of the service request.
 * @param service_len [in] The length of the service_request.
 * @param src [in] BACNET_ADDRESS of the source of the message
 * @param service_data [in] The BACNET_CONFIRMED_SERVICE_DATA information
 *                          decoded from the APDU header of this message.
 */
static void read_property_ack_handler(
    uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_ACK_DATA *service_data
)
{
    BACNET_READ_PROPERTY_DATA data;
    int len = rp_ack_decode_service_request(service_request, service_len, &data);
    if (len < 0) {
        fprintf(stderr, "Read property response decode failed!\n");
        return;
    }

    uint32_t device_id;
    bool found = address_get_device_id(src, &device_id);
    if (not found)
        return;

    if (data.object_type == OBJECT_DEVICE and data.object_property == PROP_OBJECT_LIST)
        handle_object_list(device_id, data);

    if (data.object_type == OBJECT_DEVICE and data.object_property == PROP_OBJECT_NAME)
        handle_device_name(device_id, data);
}

static void init_service_handlers(void)
{
    Device_Init(NULL);
    /* Note: this applications doesn't need to handle who-is
       it is confusing for the user! */
    /* set the handler for all the services we don't implement
       It is required to send the proper reject message... */
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    /* we must implement read property - it's required! */
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    /* handle the reply (request) coming back */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, i_am_handler);
    /* handle the data coming back from confirmed requests */
    apdu_set_confirmed_ack_handler(SERVICE_CONFIRMED_READ_PROPERTY, read_property_ack_handler);
    /* handle any errors coming back */
    apdu_set_abort_handler(MyAbortHandler);
    apdu_set_reject_handler(MyRejectHandler);
}

void bacnet_init() {
    /* check for local environment settings */
    if (getenv("BACNET_DEBUG")) {
        BACnet_Debug_Enabled = true;
    }

    Device_Set_Object_Instance_Number(BACNET_MAX_INSTANCE);
    init_service_handlers();
    address_init();
    dlenv_init();
    atexit(datalink_cleanup);
    mstimer_set(&apdu_timer, apdu_timeout() * apdu_retries());
    mstimer_set(&datalink_timer, 1000);
    BACNET_ADDRESS dest = {
        .mac_len = 0,
        .net = BACNET_BROADCAST_NETWORK,
        .len = 0
    };
    if (BACnet_Debug_Enabled) {
        fprintf(stderr, "Sending Who-Is Request");
    }
    Send_WhoIs_To_Network(&dest, -1, -1);
}

void bacnet_task() {
    BACNET_ADDRESS dest = {
        .mac_len = 0,
        .net = BACNET_BROADCAST_NETWORK,
        .len = 0
    };
    BACNET_ADDRESS src = { 0 };
    unsigned delay_milliseconds = 100;

    /* returns 0 bytes on timeout */
    uint16_t pdu_len = datalink_receive(&src, &Rx_Buf[0], MAX_MPDU, delay_milliseconds);
    /* process */
    if (pdu_len) {
        npdu_handler(&src, &Rx_Buf[0], pdu_len);
    }
    if (mstimer_expired(&datalink_timer)) {
        datalink_maintenance_timer(
            mstimer_interval(&datalink_timer) / 1000);
        mstimer_reset(&datalink_timer);
    }
    if (mstimer_expired(&apdu_timer)) {
        if (BACnet_Debug_Enabled) {
            fprintf(stderr, "Sending Who-Is Request");
        }
        Send_WhoIs_To_Network(&dest, -1, -1);
        mstimer_reset(&apdu_timer);
    }
}
