#pragma once

#include <bacnet/bactext.h>
#include <bacnet/iam.h>
#include <bacnet/basic/binding/address.h>
#include <bacnet/config.h>
#include <bacnet/bacdef.h>
#include <bacnet/npdu.h>
#include <bacnet/apdu.h>
#include <bacnet/basic/object/device.h>
#include <bacnet/datalink/datalink.h>
#include <bacnet/bactext.h>
#include <bacnet/version.h>
/* some demo stuff needed */
#include <bacnet/basic/sys/mstimer.h>
#include <bacnet/basic/sys/filename.h>
#include <bacnet/basic/services.h>
#include <bacnet/basic/services.h>
#include <bacnet/basic/tsm/tsm.h>
#include <rs485.h>
#include <bacnet/datalink/dlenv.h>
#include <bacport.h>
#include <functional>

typedef std::function<void(uint32_t, uint32_t, uint32_t, std::string)> ccov_notification_handler;

void bacnet_init(ccov_notification_handler handler);
void bacnet_task();
