/*
 * Copyright (c) 2022 ICE9 Consulting LLC
 */

#ifndef __BLADERF_H__
#define __BLADERF_H__

#include <libbladeRF.h>

int *bladerf_list(unsigned *num_out);
struct bladerf *bladerf_setup(int id);
void *bladerf_stream_thread(void *arg);

#endif
