/*
 * Unit tests for options.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "pcap.h"

// define global state variables expected by options.c
FILE *in = NULL;
char *serial = NULL;
char *usrp_serial = NULL;
int bladerf_num = -1;

float samp_rate = 0.0f;
unsigned channels = 0;
unsigned center_freq = 0;
pcap_t *pcap = NULL;
int live = 0;
int verbose = 0;
int stats = 0;

// stub functions
void usage(int exitcode) { (void)exitcode; }
void hackrf_list(void) {}
void bladerf_list(void) {}
void usrp_list(void) {}
char *usrp_get_serial(const char *interface_name) { (void)interface_name; return "123456"; }

extern int parse_options(int argc, char **argv);

static void reset_globals(void) {
    in = NULL;
    serial = NULL;
    usrp_serial = NULL;
    bladerf_num = -1;
    samp_rate = 0.0f;
    channels = 0;
    center_freq = 0;
    pcap = NULL;
    live = 0;
    verbose = 0;
    stats = 0;
}

static void test_all_channels_flag(void) {
    reset_globals();
    char *argv[] = { "ice9-bluetooth", "-a", "--capture", NULL };
    int res = parse_options(3, argv);
    assert(res == 0);
    assert(channels == 96);
    assert(center_freq == 2441);
    assert(samp_rate == 96e6f);
    assert(live == 1);
    printf("[PASS] test_all_channels_flag\n");
}

static void test_custom_channels_and_freq(void) {
    reset_globals();
    char *argv[] = { "ice9-bluetooth", "-C", "16", "-c", "2412", "--capture", NULL };
    int res = parse_options(6, argv);
    assert(res == 0);
    assert(channels == 16);
    assert(center_freq == 2412);
    assert(samp_rate == 16e6f);
    assert(live == 1);
    printf("[PASS] test_custom_channels_and_freq\n");
}

static void test_invalid_center_freq(void) {
    reset_globals();
    char *argv[] = { "ice9-bluetooth", "-C", "16", "-c", "2300", "--capture", NULL };
    int res = parse_options(6, argv);
    assert(res == -1);
    printf("[PASS] test_invalid_center_freq\n");
}

static void test_invalid_channels_not_divisible_by_4(void) {
    reset_globals();
    char *argv[] = { "ice9-bluetooth", "-C", "15", "-c", "2441", "--capture", NULL };
    int res = parse_options(6, argv);
    assert(res == -1);
    printf("[PASS] test_invalid_channels_not_divisible_by_4\n");
}

static void test_extcap_interfaces_flag(void) {
    reset_globals();
    char *argv[] = { "ice9-bluetooth", "--extcap-interfaces", NULL };
    int res = parse_options(2, argv);
    assert(res == 1);
    printf("[PASS] test_extcap_interfaces_flag\n");
}

int main(void) {
    printf("===========================================\n");
    printf(" Running options.c Unit Tests              \n");
    printf("===========================================\n");
    test_all_channels_flag();
    test_custom_channels_and_freq();
    test_invalid_center_freq();
    test_invalid_channels_not_divisible_by_4();
    test_extcap_interfaces_flag();
    printf("===========================================\n");
    printf(" All options tests passed successfully!    \n");
    printf("===========================================\n");
    return 0;
}
