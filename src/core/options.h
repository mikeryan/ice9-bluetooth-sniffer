/*
 * Copyright 2022 ICE9 Consulting LLC
 */

#ifndef __OPTIONS_H__
#define __OPTIONS_H__

#include <stdio.h>
#include <stdint.h>

#include "pcap.h"

typedef struct {
    FILE *in;
    char *serial;
    char *usrp_serial;
    int bladerf_num;

    float samp_rate;
    unsigned channels;
    unsigned center_freq;
    pcap_t *pcap;
    int live;
    int verbose;
    int stats;

    char *dump_path;
    FILE *dump_file;
    int dump_only;
} sniffer_config_t;

extern sniffer_config_t config;

void config_init(sniffer_config_t *cfg);
void config_free(sniffer_config_t *cfg);
int parse_options(int argc, char **argv, sniffer_config_t *cfg);

#endif /* __OPTIONS_H__ */
