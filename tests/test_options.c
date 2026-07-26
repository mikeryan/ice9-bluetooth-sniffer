/*
 * Unit tests for options.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "options.h"

// stub functions
void usage(int exitcode) { (void)exitcode; }
#ifdef HAVE_HACKRF
void hackrf_list(void) {}
#endif
#ifdef HAVE_BLADERF
void bladerf_list(void) {}
#endif
#ifdef HAVE_UHD
void usrp_list(void) {}
char *usrp_get_serial(const char *interface_name) { (void)interface_name; return "31215"; }
#endif

static void test_all_channels_flag(void) {
    sniffer_config_t cfg;
    char *argv[] = { "ice9-bluetooth", "-a", "--capture", NULL };
    int res = parse_options(3, argv, &cfg);
    assert(res == 0);
    assert(cfg.channels == 96);
    assert(cfg.center_freq == 2441);
    assert(cfg.samp_rate == 96e6f);
    assert(cfg.live == 1);
    config_free(&cfg);
    printf("[PASS] test_all_channels_flag\n");
}

static void test_custom_channels_and_freq(void) {
    sniffer_config_t cfg;
    char *argv[] = { "ice9-bluetooth", "-C", "16", "-c", "2412", "--capture", NULL };
    int res = parse_options(6, argv, &cfg);
    assert(res == 0);
    assert(cfg.channels == 16);
    assert(cfg.center_freq == 2412);
    assert(cfg.samp_rate == 16e6f);
    assert(cfg.live == 1);
    config_free(&cfg);
    printf("[PASS] test_custom_channels_and_freq\n");
}

static void test_sdr_interface_selection(void) {
    sniffer_config_t cfg;
    char *argv1[] = { "ice9-bluetooth", "-a", "-i", "hackrf-12345", "--capture", NULL };
    int res = parse_options(5, argv1, &cfg);
#ifdef HAVE_HACKRF
    assert(res == 0);
    assert(cfg.serial != NULL && strcmp(cfg.serial, "12345") == 0);
    config_free(&cfg);
#else
    assert(res == -1);
#endif

    char *argv2[] = { "ice9-bluetooth", "-a", "-i", "bladerf0", "--capture", NULL };
    res = parse_options(5, argv2, &cfg);
#ifdef HAVE_BLADERF
    assert(res == 0);
    assert(cfg.bladerf_num == 0);
    config_free(&cfg);
#else
    assert(res == -1);
#endif

    char *argv3[] = { "ice9-bluetooth", "-a", "-i", "usrp-31215", "--capture", NULL };
    res = parse_options(5, argv3, &cfg);
#ifdef HAVE_UHD
    assert(res == 0);
    assert(cfg.usrp_serial != NULL && strcmp(cfg.usrp_serial, "31215") == 0);
    config_free(&cfg);
#else
    assert(res == -1);
#endif

    printf("[PASS] test_sdr_interface_selection\n");
}

static void test_invalid_center_freq(void) {
    sniffer_config_t cfg;
    char *argv[] = { "ice9-bluetooth", "-C", "16", "-c", "2300", "--capture", NULL };
    int res = parse_options(6, argv, &cfg);
    assert(res == -1);
    config_free(&cfg);
    printf("[PASS] test_invalid_center_freq\n");
}

static void test_invalid_channels_not_divisible_by_4(void) {
    sniffer_config_t cfg;
    char *argv[] = { "ice9-bluetooth", "-C", "15", "-c", "2441", "--capture", NULL };
    int res = parse_options(6, argv, &cfg);
    assert(res == -1);
    config_free(&cfg);
    printf("[PASS] test_invalid_channels_not_divisible_by_4\n");
}

static void test_extcap_interfaces_flag(void) {
    sniffer_config_t cfg;
    char *argv[] = { "ice9-bluetooth", "--extcap-interfaces", NULL };
    int res = parse_options(2, argv, &cfg);
    assert(res == 1);
    config_free(&cfg);
    printf("[PASS] test_extcap_interfaces_flag\n");
}

int main(void) {
    printf("===========================================\n");
    printf(" Running options.c Unit Tests              \n");
    printf("===========================================\n");
    test_all_channels_flag();
    test_custom_channels_and_freq();
    test_sdr_interface_selection();
    test_invalid_center_freq();
    test_invalid_channels_not_divisible_by_4();
    test_extcap_interfaces_flag();
    printf("===========================================\n");
    printf(" All options tests passed successfully!    \n");
    printf("===========================================\n");
    return 0;
}
