/*
 * Unit tests for pcap.c / pcap.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "pcap.h"

typedef struct __attribute__((packed)) {
    uint32_t magic_number;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t  thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
} expected_pcap_hdr_t;

typedef struct __attribute__((packed)) {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
} expected_pcaprec_hdr_t;

typedef struct __attribute__((packed)) {
    uint8_t rf_channel;
    int8_t signal_power;
    int8_t noise_power;
    uint8_t aa_offenses;
    uint32_t ref_aa;
    uint16_t flags;
} expected_pcap_le_header_t;

static void test_pcap_write_and_parse(void) {
    const char *test_file = "test_output.pcap";
    pcap_t *p = pcap_open((char*)test_file);
    assert(p != NULL);

    ble_packet_t *pkt = malloc(sizeof(ble_packet_t) + 4);
    memset(pkt, 0, sizeof(*pkt) + 4);
    pkt->freq = 2402; // (2402 - 2402) / 2 = channel 0
    pkt->rssi_db = -50;
    pkt->noise_db = -90;
    pkt->timestamp.tv_sec = 1600000000;
    pkt->timestamp.tv_nsec = 500000000; // 500ms -> 500000 us

    pkt->len = 4;
    pkt->data[0] = 0xAA;
    pkt->data[1] = 0xBB;
    pkt->data[2] = 0xCC;
    pkt->data[3] = 0xDD;

    pcap_write_ble(p, pkt);
    free(pkt);
    pcap_close(p);

    // Read written file back and verify PCAP header, record header, LE header, and payload
    FILE *f = fopen(test_file, "rb");
    assert(f != NULL);

    expected_pcap_hdr_t hdr;
    assert(fread(&hdr, sizeof(hdr), 1, f) == 1);
    assert(hdr.magic_number == 0xa1b2c3d4);
    assert(hdr.version_major == 2);
    assert(hdr.version_minor == 4);
    assert(hdr.network == 256); // DLT_BLUETOOTH_LE_LL_WITH_PHDR

    expected_pcaprec_hdr_t rechdr;
    assert(fread(&rechdr, sizeof(rechdr), 1, f) == 1);
    assert(rechdr.ts_sec == 1600000000);
    assert(rechdr.ts_usec == 500000);
    assert(rechdr.incl_len == 4 + sizeof(expected_pcap_le_header_t));
    assert(rechdr.orig_len == 4 + sizeof(expected_pcap_le_header_t));

    expected_pcap_le_header_t le_hdr;
    assert(fread(&le_hdr, sizeof(le_hdr), 1, f) == 1);
    assert(le_hdr.rf_channel == 0);
    assert(le_hdr.signal_power == -50);
    assert(le_hdr.noise_power == -90);

    uint8_t payload[4];
    assert(fread(payload, 1, 4, f) == 4);
    assert(payload[0] == 0xAA && payload[1] == 0xBB && payload[2] == 0xCC && payload[3] == 0xDD);

    fclose(f);
    remove(test_file);
    printf("[PASS] test_pcap_write_and_parse\n");
}

int main(void) {
    printf("===========================================\n");
    printf(" Running pcap.c Unit Tests                 \n");
    printf("===========================================\n");
    test_pcap_write_and_parse();
    printf("===========================================\n");
    printf(" All pcap tests passed successfully!       \n");
    printf("===========================================\n");
    return 0;
}
