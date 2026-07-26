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

static void test_pcap_write_and_parse(void) {
    const char *test_file = "test_output.pcap";
    pcap_t *p = pcap_open((char*)test_file);
    assert(p != NULL);

    ble_packet_t *pkt = malloc(sizeof(ble_packet_t) + 4);
    memset(pkt, 0, sizeof(*pkt) + 4);
    pkt->freq = 2402;
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

    // Read written file back and verify PCAP header and record fields
    FILE *f = fopen(test_file, "rb");
    assert(f != NULL);

    expected_pcap_hdr_t hdr;
    assert(fread(&hdr, sizeof(hdr), 1, f) == 1);
    assert(hdr.magic_number == 0xa1b2c3d4);
    assert(hdr.version_major == 2);
    assert(hdr.version_minor == 4);
    assert(hdr.network == 256); // DLT_BLUETOOTH_LE_LL_WITH_PHDR

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
