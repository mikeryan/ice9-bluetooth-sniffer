/*
 * Copyright (c) 2022 ICE9 Consulting LLC
 */

#ifndef __PCAP_H__
#define __PCAP_H__

#include "bluetooth.h"

typedef struct _pcap_t pcap_t;

pcap_t *pcap_open(char *path);
void pcap_close(pcap_t *p);
void pcap_write_ble(pcap_t *p, ble_packet_t *b);

#endif
