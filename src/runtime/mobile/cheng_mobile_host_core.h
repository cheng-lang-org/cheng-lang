#ifndef MOBILE_HOST_CORE_H
#define MOBILE_HOST_CORE_H

#include "cheng_mobile_bridge.h"

void cheng_mobile_host_core_reset(void);
int cheng_mobile_host_core_push(const ChengMobileEvent* ev);
int cheng_mobile_host_core_pop(ChengMobileEvent* out);

#endif
