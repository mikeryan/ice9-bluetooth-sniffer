/*
 * Copyright (c) 2022 ICE9 Consulting LLC
 */

#include <stdio.h>
#include <stdlib.h>

#include "pcap.h"

struct _pcap_t {
    FILE *f;
};

typedef struct __attribute__((packed)) _pcap_hdr_t {
    uint32_t magic_number;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t  thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
} pcap_hdr_t;

typedef struct __attribute__((packed)) _pcaprec_hdr_t {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
} pcaprec_hdr_t;

typedef struct __attribute__((packed)) _pcap_le_header_t {
    uint8_t rf_channel;
    int8_t signal_power;
    int8_t noise_power;
    uint8_t aa_offenses;
    uint32_t ref_aa;
    uint16_t flags;
} pcap_le_header_t;

#define LE_DEWHITENED 0x0001

#if !defined( DLT_BLUETOOTH_LE_LL_WITH_PHDR )
#define DLT_BLUETOOTH_LE_LL_WITH_PHDR 256
#endif

pcap_t *pcap_open(char *path) {
    pcap_t *p;
    pcap_hdr_t h = {
        .magic_number = 0xa1b2c3d4,
        .version_major = 2,
        .version_minor = 4,
        .snaplen = 4 + 2 + 255 + 3,
        .network = DLT_BLUETOOTH_LE_LL_WITH_PHDR,
    };

    FILE *f = fopen(path, "w");
    if (f == NULL)
        return NULL;
    p = malloc(sizeof(*p));
    p->f = f;

    // write header
    fwrite(&h, sizeof(h), 1, p->f);

    return p;
}

void pcap_close(pcap_t *p) {
    fclose(p->f);
    free(p);
}

// TODO timestamp
void pcap_write_ble(pcap_t *p, ble_packet_t *b) {
    pcap_le_header_t le_header = {
        .rf_channel = (b->freq - 2402) / 2,
        .flags = LE_DEWHITENED,
    };
    pcaprec_hdr_t pcap_header = {
        .ts_sec   = b->timestamp.tv_sec,
        .ts_usec  = b->timestamp.tv_nsec/1000,
        .incl_len = b->len + sizeof(le_header),
        .orig_len = b->len + sizeof(le_header),
    };
    fwrite(&pcap_header, sizeof(pcap_header), 1, p->f);
    fwrite(&le_header, sizeof(le_header), 1, p->f);
    fwrite(b->data, b->len, 1, p->f);
    fflush(p->f);
}
