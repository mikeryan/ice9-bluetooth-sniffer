/*
 * Copyright (c) 2022 ICE9 Consulting LLC
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <btbb.h>

#include "bluetooth.h"

extern lell_pcap_handle *pcap;

typedef struct _ble_packet_t {
    uint32_t aa;
    unsigned len; // length including AA + header + CRC
    uint8_t data[0]; // data starts at AA
} ble_packet_t;

static const uint8_t whitening[] = {
    1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 0,
    1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1,
    0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0, 0, 1, 0, 1, 0, 0, 1,
    0, 1,
};

static const uint8_t whitening_index[] = {
    70, 62, 120, 111, 77, 46, 15, 101, 66, 39, 31, 26, 80, 83, 125, 89, 10, 35,
    8, 54, 122, 17, 33, 0, 58, 115, 6, 94, 86, 49, 52, 20, 40, 27, 84, 90, 63,
    112, 47, 102,
};

static unsigned freq_to_channel(unsigned freq) {
    unsigned phys_channel = (freq - 2402) / 2;
    if (phys_channel == 0) return 37;
    if (phys_channel == 12) return 38;
    if (phys_channel == 39) return 39;
    if (phys_channel < 12) return phys_channel - 1;
    return phys_channel - 2;
}

ble_packet_t *ble_burst(uint8_t *bits, unsigned bits_len, unsigned freq) {
    unsigned i, j;
    // unsigned burst_len = (unsigned)roundf((float)bits_len / 8.0f);
    unsigned smallest_delta = 0xffffffff;
    unsigned smallest_offset = 0;
    uint32_t smallest_aa = 0;
    uint8_t smallest_header_len;

    // possibly BLE, extract access address
    if (bits[0] == bits[2] && bits[2] == bits[4] &&
            bits[1] == bits[3] && bits[3] == bits[5]) {
        unsigned channel = freq_to_channel(freq);

        // try three candidates for AA
        for (i = 6; i < 9; ++i) {
            uint32_t aa = 0;
            for (j = 0; j < 32; ++j)
                aa |= bits[i+j] << j;
            uint8_t header_len = 0;
            unsigned wh = (whitening_index[channel] + 8) % sizeof(whitening);
            for (j = 0; j < 8; ++j) {
                header_len |= (bits[i+32+8+j] ^ whitening[wh]) << j;
                wh = (wh + 1) % sizeof(whitening);
            }
            unsigned bit_len = 8 + 32 + 16 + header_len * 8 + 24; // preamble + AA + header + body + CRC
            int delta = (int)bits_len - (int)bit_len;
            if (delta > 0 && delta < smallest_delta) {
                smallest_delta = delta;
                smallest_offset = i;
                smallest_aa = aa;
                smallest_header_len = header_len;
            }
        }

        // see if any of the candidates have a length that makes sense
        if (smallest_delta < 20) {
#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))
            ble_packet_t *p = malloc(sizeof(*p) + MAX(4 + 2 + smallest_header_len + 3, 64)); // FIXME bug in libbtbb
            p->aa = smallest_aa;
            p->len = 2 + smallest_header_len + 3;
            unsigned wh = whitening_index[channel];
            p->data[0] = (smallest_aa >>  0) & 0xff;
            p->data[1] = (smallest_aa >>  8) & 0xff;
            p->data[2] = (smallest_aa >> 16) & 0xff;
            p->data[3] = (smallest_aa >> 24) & 0xff;
            for (i = 0; i < p->len-4; ++i) {
                uint8_t byte = 0;
                for (j = 0; j < 8; ++j) {
                    byte |= (bits[smallest_offset+32+i*8+j] ^ whitening[wh]) << j;
                    wh = (wh + 1) % sizeof(whitening);
                }
                p->data[i+4] = byte;
            }
            return p;
        }
    }
    return NULL;
}

void bluetooth_detect(uint8_t *bits, unsigned len, unsigned freq, uint32_t *lap_out, uint32_t *aa_out) {
    btbb_packet *p = NULL;
    int r = btbb_find_ac((char *)bits, len, LAP_ANY, 1, &p);
    if (r >= 0) {
        *lap_out = btbb_packet_get_lap(p);
        /*
        btbb_packet_set_data(p, (char *)bits + r, len - r, 0, 0); // TODO channel
        r = btbb_decode(p);
        */
        btbb_packet_unref(p);
    } else {
        ble_packet_t * p = ble_burst(bits, len, freq);
        if (p != NULL) {
            *aa_out = p->aa;
            if (pcap) {
                lell_packet *pkt;
                lell_allocate_and_decode(p->data, freq, 0, &pkt);
                lell_pcap_append_packet(pcap, 0, 0, 0, p->aa, pkt);
                lell_packet_unref(pkt);
            }

#if 0
            aa_stats_t *aa_stats = hash_find(aa_hash, p->aa);
            if (aa_stats == NULL) {
                aa_stats = calloc(1, sizeof(*aa));
                hash_insert(aa_hash, aa, aa_stats);
            }
            ++aa_stats->count;
#endif

            free(p);
        }
    }
}
