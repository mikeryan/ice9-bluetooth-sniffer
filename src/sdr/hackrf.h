/*
 * Copyright 2022 ICE9 Consulting LLC
 */

#ifndef __OUR_HACKRF_H__
#define __OUR_HACKRF_H__

#include <libhackrf/hackrf.h>

void hackrf_list(void);
hackrf_device *hackrf_setup(void);
int hackrf_rx_cb(hackrf_transfer *t);

#endif
