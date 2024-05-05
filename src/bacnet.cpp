#include "bacnet.hpp"
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <chrono>

/* buffer used for receive */
static uint8_t Rx_Buf[MAX_MPDU] = { 0 };

/* debug info printing */
static bool BACnet_Debug_Enabled;

/* timers */
static struct mstimer subscription_timer = { 0 }; 
static struct mstimer who_is_timer = { 0 }; 
static struct mstimer datalink_timer = { 0 };
static struct mstimer tsm_timer = { 0 };
static ccov_notification_handler ccov_notification_handler_ = nullptr;

struct device_entry {
    std::vector<BACNET_OBJECT_ID> object_list;
};
std::unordered_map<uint32_t, device_entry> device_map;

struct cov_entry {
    std::chrono::time_point<std::chrono::system_clock> subscribe_end;
    long tag;
};
template <>
struct std::hash<std::pair<uint32_t, BACNET_OBJECT_ID>>
{
  std::size_t operator()(const std::pair<uint32_t, BACNET_OBJECT_ID>& k) const
  {
    return std::hash<uint32_t>()(k.first) ^ std::hash<int>()(static_cast<int>(k.second.type)) ^ std::hash<uint32_t>()(k.second.instance);
  }
};
bool operator==(const BACNET_OBJECT_ID& lhs, const BACNET_OBJECT_ID& rhs) {
    return lhs.type == rhs.type and lhs.instance == rhs.instance;
}
std::unordered_map<std::pair<uint32_t, BACNET_OBJECT_ID>, cov_entry> cov_map;

static void device_map_add(uint32_t device_id)
{
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

std::string application_data_value_to_string(const BACNET_APPLICATION_DATA_VALUE& value) {
    switch (value.tag) {
        case BACNET_APPLICATION_TAG_BOOLEAN:
            return std::to_string(value.type.Boolean);
        case BACNET_APPLICATION_TAG_UNSIGNED_INT:
            return std::to_string(value.type.Unsigned_Int);
        case BACNET_APPLICATION_TAG_SIGNED_INT:
            return std::to_string(value.type.Signed_Int);
        case BACNET_APPLICATION_TAG_REAL:
            return std::to_string(value.type.Real);
        case BACNET_APPLICATION_TAG_DOUBLE:
            return std::to_string(value.type.Double);
        case BACNET_APPLICATION_TAG_OCTET_STRING:
            return std::string(reinterpret_cast<const char*>(value.type.Octet_String.value), value.type.Octet_String.length);
        case BACNET_APPLICATION_TAG_CHARACTER_STRING:
            return std::string(value.type.Character_String.value, value.type.Character_String.length);
        case BACNET_APPLICATION_TAG_BIT_STRING:
            return std::string(reinterpret_cast<const char*>(value.type.Bit_String.value), value.type.Bit_String.bits_used);
        case BACNET_APPLICATION_TAG_ENUMERATED:
            return std::to_string(value.type.Enumerated);
        default:
            return "";
    }
}

static void ccov_notification_handle(BACNET_COV_DATA *cov_data)
{
    auto cov_key = std::make_pair(cov_data->initiatingDeviceIdentifier, cov_data->monitoredObjectIdentifier);
    auto subscribe_end = std::chrono::system_clock::now() + std::chrono::seconds(cov_data->timeRemaining);
    cov_map[cov_key] = {.subscribe_end = subscribe_end};
    for (auto *property_value = cov_data->listOfValues; property_value != nullptr; property_value = property_value->next) {
        if (property_value->propertyIdentifier == PROP_PRESENT_VALUE) {
            cov_map[cov_key] = {.subscribe_end = subscribe_end, .tag = property_value->value.tag};
            if (ccov_notification_handler_) {
                ccov_notification_handler_(
                    cov_data->initiatingDeviceIdentifier,
                    bactext_object_type_name(cov_data->monitoredObjectIdentifier.type),
                    cov_data->monitoredObjectIdentifier.instance,
                    application_data_value_to_string(property_value->value)
                );
            }
        }
    }

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
            for (const auto& object_id: object_list) {
                if (object_id.type == OBJECT_DEVICE)
                    continue;
                cov_map[std::make_pair(device_id, object_id)] = {.subscribe_end = std::chrono::time_point<std::chrono::system_clock>()};
		auto cov_key = std::make_pair(device_id, object_id);
                if (BACnet_Debug_Enabled) {
                    fprintf(
                        stderr,
                        "Sending COV subscribe to: %d, type: %s, instance %d\n",
                        cov_key.first,
                        bactext_object_type_name(cov_key.second.type),
                        cov_key.second.instance
                    );
                }
                BACNET_SUBSCRIBE_COV_DATA cov_data = {
                    .subscriberProcessIdentifier = cov_key.first,
                    .monitoredObjectIdentifier = cov_key.second,
                    .cancellationRequest = false,
                    .issueConfirmedNotifications = true,
                    .lifetime = 300
                };
                Send_COV_Subscribe(cov_key.first, &cov_data);
            }
            device_map[device_id] = {.object_list = std::move(object_list)};
            if (BACnet_Debug_Enabled) {
                fprintf(stderr, "Adding new device entry for device %d\n", device_id);
            }
            break;
        }
    }
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
}

static void handler_subscribe_ccov_ack(
    BACNET_ADDRESS *src, uint8_t invoke_id)
{
    uint32_t device_id;
    bool found = address_get_device_id(src, &device_id);
    if (not found)
        return;

    fprintf(stderr, "SubscribeCOV Acknowledged from: %x\n", src->mac[0]);
}

static void MyErrorHandler(
    BACNET_ADDRESS *src,
    uint8_t invoke_id,
    BACNET_ERROR_CLASS error_class,
    BACNET_ERROR_CODE error_code
)
{
    fprintf(
        stderr,
        "BACnet Error (invoke id %d): %s: %s\n",
        invoke_id,
        bactext_error_class_name(static_cast<int>(error_class)),
        bactext_error_code_name(static_cast<int>(error_code))
    );
}

static void MyAbortHandler(
    BACNET_ADDRESS *src,
    uint8_t invoke_id,
    uint8_t abort_reason,
    bool server
)
{
    /* FIXME: verify src and invoke id */
    (void)src;
    (void)invoke_id;
    (void)server;
    fprintf(stderr, "BACnet Abort: %s\n", bactext_abort_reason_name(abort_reason));
}

static void MyRejectHandler(
    BACNET_ADDRESS *src,
    uint8_t invoke_id,
    uint8_t reject_reason
)
{
    /* FIXME: verify src and invoke id */
    (void)src;
    (void)invoke_id;
    fprintf(stderr, "BACnet Reject: %s\n", bactext_reject_reason_name(reject_reason));
}

void TsmTimeoutHandler(uint8_t invoke_id)
{
    fprintf(stderr, "BACnet Timeout: %d\n", invoke_id);
}

static void init_service_handlers(void)
{
    static BACNET_COV_NOTIFICATION ccov_cb = {.next = nullptr, .callback = ccov_notification_handle};
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
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_COV_NOTIFICATION, handler_ccov_notification);
    handler_ccov_notification_add(&ccov_cb);
    /* handle the data coming back from confirmed requests */
    apdu_set_confirmed_ack_handler(SERVICE_CONFIRMED_READ_PROPERTY, read_property_ack_handler);
    apdu_set_confirmed_simple_ack_handler(SERVICE_CONFIRMED_SUBSCRIBE_COV, handler_subscribe_ccov_ack);
    /* handle any errors coming back */
    apdu_set_error_handler(SERVICE_CONFIRMED_READ_PROPERTY, MyErrorHandler);
    apdu_set_error_handler(SERVICE_CONFIRMED_SUBSCRIBE_COV, MyErrorHandler);
    apdu_set_abort_handler(MyAbortHandler);
    apdu_set_reject_handler(MyRejectHandler);
    tsm_set_timeout_handler(TsmTimeoutHandler);
}

void renew_subscriptions() {
    std::unordered_set<uint32_t> handeled_devices;
    for (const auto& [cov_key, cov_entry]: cov_map) {
        if (handeled_devices.count(cov_key.first)) {
            continue;
        }
        if (cov_entry.subscribe_end - std::chrono::minutes(1) <= std::chrono::system_clock::now()) {
            if (BACnet_Debug_Enabled) {
                fprintf(
                    stderr,
                    "Sending COV subscribe to: %d, type: %s, instance %d\n",
                    cov_key.first,
                    bactext_object_type_name(cov_key.second.type),
                    cov_key.second.instance
                );
            }
            BACNET_SUBSCRIBE_COV_DATA cov_data = {
                .subscriberProcessIdentifier = cov_key.first,
                .monitoredObjectIdentifier = cov_key.second,
                .cancellationRequest = false,
                .issueConfirmedNotifications = true,
                .lifetime = 300
            };
            Send_COV_Subscribe(cov_key.first, &cov_data);
            handeled_devices.insert(cov_key.first);
        }
    }
}

void bacnet_init(ccov_notification_handler handler) {
    /* check for local environment settings */
    if (getenv("BACNET_DEBUG")) {
        BACnet_Debug_Enabled = true;
    }

    Device_Set_Object_Instance_Number(BACNET_MAX_INSTANCE);
    init_service_handlers();
    address_init();
    dlenv_init();
    atexit(datalink_cleanup);
    mstimer_set(&subscription_timer, 5000);
    mstimer_set(&who_is_timer, 10 * apdu_timeout() * apdu_retries());
    mstimer_set(&datalink_timer, 1000);
    mstimer_set(&tsm_timer, 100);
    ccov_notification_handler_ = handler;
    BACNET_ADDRESS dest = {
        .mac_len = 0,
        .net = BACNET_BROADCAST_NETWORK,
        .len = 0
    };
    if (BACnet_Debug_Enabled) {
        fprintf(stderr, "Sending Who-Is Request\n");
    }
    Send_WhoIs_To_Network(&dest, -1, -1);
}

void bacnet_task() {
    BACNET_ADDRESS src = { 0 };
    unsigned delay_milliseconds = 100;

    /* returns 0 bytes on timeout */
    uint16_t pdu_len = datalink_receive(&src, &Rx_Buf[0], MAX_MPDU, delay_milliseconds);
    /* process */
    if (pdu_len) {
        npdu_handler(&src, &Rx_Buf[0], pdu_len);
    }
    if (mstimer_expired(&tsm_timer)) {
        tsm_timer_milliseconds(mstimer_interval(&tsm_timer));
        mstimer_reset(&tsm_timer);
    }
    if (mstimer_expired(&datalink_timer)) {
        datalink_maintenance_timer(mstimer_interval(&datalink_timer) / 1000);
        mstimer_reset(&datalink_timer);
    }
    if (mstimer_expired(&subscription_timer)) {
        mstimer_reset(&subscription_timer);
        renew_subscriptions();
    }
    if (mstimer_expired(&who_is_timer)) {
        mstimer_reset(&who_is_timer);
        if (BACnet_Debug_Enabled) {
            fprintf(stderr, "Sending Who-Is Request\n");
        }
        BACNET_ADDRESS dest = {
            .mac_len = 0,
            .net = BACNET_BROADCAST_NETWORK,
            .len = 0
        };
        Send_WhoIs_To_Network(&dest, -1, -1);
    }
}

void bacnet_send(uint32_t id, const std::string& type, uint32_t instance, std::string value) {
    unsigned type_int;
    if (not bactext_object_type_strtol(type.c_str(), &type_int)) {
        fprintf(stderr, "Unknown type for id: %d, type: %s, instance: %d\n", id, type.c_str(), instance);
    }
    auto cov_key = std::make_pair(id, BACNET_OBJECT_ID{.type = BACnetObjectType(type_int), .instance = instance});
    auto cov_entry_it = cov_map.find(cov_key);
    if (cov_entry_it == cov_map.end()) {
        fprintf(stderr, "Unknown property for id: %d, type: %s, instance: %d\n", id, type.c_str(), instance);
        return;
    }
    BACNET_APPLICATION_DATA_VALUE data_value = {};
    if (
        !bacapp_parse_application_data(
            static_cast<BACNET_APPLICATION_TAG>(cov_entry_it->second.tag),
            const_cast<char *>(value.c_str()), &data_value)
    ) {
        fprintf(
            stderr,
            "Error: unable to parse the value %s, for id: %d, type: %s, instance: %d\n",
            value.c_str(),
            id,
            type.c_str(),
            instance
        );
        return;
    }
    Send_Write_Property_Request(
        id,
        BACnetObjectType(type_int),
        instance,
        PROP_PRESENT_VALUE,
        &data_value,
        0,
        BACNET_ARRAY_ALL
    );
}
