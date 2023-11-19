#include <cstdint>
#include <iostream>
#include "bacnet.h"

/* buffer used for receive */
static uint8_t Rx_Buf[MAX_MPDU] = { 0 };

/* debug info printing */
static bool BACnet_Debug_Enabled;

/* timers */
static struct mstimer apdu_timer = { 0 }; 
static struct mstimer datalink_timer = { 0 };

#define BAC_ADDRESS_MULT 1

//TODO: replace with c++ way
struct address_entry {
    struct address_entry *next;
    uint8_t Flags;
    uint32_t device_id;
    unsigned max_apdu;
    BACNET_ADDRESS address;
};

static struct address_table {
    struct address_entry *first;
    struct address_entry *last;
} Address_Table = { 0 };

static struct address_entry *alloc_address_entry(void)
{
    struct address_entry *rval;
    rval = (struct address_entry *)calloc(1, sizeof(struct address_entry));
    if (Address_Table.first == 0) {
        Address_Table.first = Address_Table.last = rval;
    } else {
        Address_Table.last->next = rval;
        Address_Table.last = rval;
    }
    return rval;
}

static void address_table_add(
    uint32_t device_id, unsigned max_apdu, BACNET_ADDRESS *src)
{
    struct address_entry *pMatch;
    uint8_t flags = 0;

    pMatch = Address_Table.first;
    while (pMatch) {
        if (pMatch->device_id == device_id) {
            if (bacnet_address_same(&pMatch->address, src)) {
                return;
            }
            flags |= BAC_ADDRESS_MULT;
            pMatch->Flags |= BAC_ADDRESS_MULT;
        }
        pMatch = pMatch->next;
    }

    pMatch = alloc_address_entry();

    pMatch->Flags = flags;
    pMatch->device_id = device_id;
    pMatch->max_apdu = max_apdu;
    pMatch->address = *src;

    return;
}

static void my_i_am_handler(
    uint8_t *service_request, uint16_t service_len, BACNET_ADDRESS *src)
{
    int len = 0;
    uint32_t device_id = 0;
    unsigned max_apdu = 0;
    int segmentation = 0;
    uint16_t vendor_id = 0;
    unsigned i = 0;

    (void)service_len;
    len = iam_decode_service_request(
        service_request, &device_id, &max_apdu, &segmentation, &vendor_id);
    if (BACnet_Debug_Enabled) {
        fprintf(stderr, "Received I-Am Request");
    }
    if (len != -1) {
        if (BACnet_Debug_Enabled) {
            fprintf(stderr, " from %lu, MAC = ", (unsigned long)device_id);
            if ((src->mac_len == 6) && (src->len == 0)) {
                fprintf(stderr, "%u.%u.%u.%u %02X%02X\n", (unsigned)src->mac[0],
                    (unsigned)src->mac[1], (unsigned)src->mac[2],
                    (unsigned)src->mac[3], (unsigned)src->mac[4],
                    (unsigned)src->mac[5]);
            } else {
                for (i = 0; i < src->mac_len; i++) {
                    fprintf(stderr, "%02X", (unsigned)src->mac[i]);
                    if (i < (src->mac_len - 1)) {
                        fprintf(stderr, ":");
                    }
                }
                fprintf(stderr, "\n");
            }
        }
        address_table_add(device_id, max_apdu, src);
    } else {
        if (BACnet_Debug_Enabled) {
            fprintf(stderr, ", but unable to decode it.\n");
        }
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

static void init_service_handlers(void)
{
    Device_Init(NULL);
    /* Note: this applications doesn't need to handle who-is
       it is confusing for the user! */
    /* set the handler for all the services we don't implement
       It is required to send the proper reject message... */
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    /* we must implement read property - it's required! */
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    /* handle the reply (request) coming back */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, my_i_am_handler);
    /* handle any errors coming back */
    apdu_set_abort_handler(MyAbortHandler);
    apdu_set_reject_handler(MyRejectHandler);
}

//TODO: replace with c++ way
static void print_macaddr(uint8_t *addr, int len)
{
    int j = 0;

    while (j < len) {
        if (j != 0) {
            printf(":");
        }
        printf("%02X", addr[j]);
        j++;
    }
    while (j < MAX_MAC_LEN) {
        printf("   ");
        j++;
    }
}

//TODO: replace with c++ way
static void print_address_cache(void)
{
    BACNET_ADDRESS address;
    unsigned total_addresses = 0;
    unsigned dup_addresses = 0;
    struct address_entry *addr;
    uint8_t local_sadr = 0;

    /*  NOTE: this string format is parsed by src/address.c,
       so these must be compatible. */

    printf(";%-7s  %-20s %-5s %-20s %-4s\n", "Device", "MAC (hex)", "SNET",
        "SADR (hex)", "APDU");
    printf(";-------- -------------------- ----- -------------------- ----\n");

    addr = Address_Table.first;
    while (addr) {
        bacnet_address_copy(&address, &addr->address);
        total_addresses++;
        if (addr->Flags & BAC_ADDRESS_MULT) {
            dup_addresses++;
            printf(";");
        } else {
            printf(" ");
        }
        printf(" %-7u ", addr->device_id);
        print_macaddr(address.mac, address.mac_len);
        printf(" %-5hu ", address.net);
        if (address.net) {
            print_macaddr(address.adr, address.len);
        } else {
            print_macaddr(&local_sadr, 1);
        }
        printf(" %-4u ", (unsigned)addr->max_apdu);
        printf("\n");

        addr = addr->next;
    }
    printf(";\n; Total Devices: %u\n", total_addresses);
    if (dup_addresses) {
        printf("; * Duplicate Devices: %u\n", dup_addresses);
    }
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
        Send_WhoIs_To_Network(&dest, -1, -1);
        mstimer_reset(&apdu_timer);
        print_address_cache();
    }
}
