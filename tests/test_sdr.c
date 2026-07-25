/*
 * Unit tests for unified SDR Hardware Abstraction Layer (HAL)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>

#include "sdr.h"
#include "options.h"

// Stubs for options.c dependencies in test_sdr executable
void usage(int exitcode) { (void)exitcode; }
char *usrp_get_serial(const char *interface_name) { (void)interface_name; return "123456"; }
void hackrf_list(void) {}
void bladerf_list(void) {}
void usrp_list(void) {}

// Mock SDR driver state tracking
static int mock_open(sdr_dev_t *dev, const sniffer_config_t *cfg) {
    (void)cfg;
    dev->priv = (void*)0xDEADBEEF;
    return 0;
}
static int mock_start(sdr_dev_t *dev) { dev->is_streaming = true; return 0; }
static int mock_is_streaming(sdr_dev_t *dev) { return dev->is_streaming ? 1 : 0; }
static int mock_stop(sdr_dev_t *dev) { dev->is_streaming = false; return 0; }
static void mock_close(sdr_dev_t *dev) { dev->priv = NULL; }

const sdr_ops_t hackrf_sdr_ops = {
    .name = "hackrf",
    .open = mock_open,
    .start = mock_start,
    .is_streaming = mock_is_streaming,
    .stop = mock_stop,
    .close = mock_close,
};

const sdr_ops_t bladerf_sdr_ops = {
    .name = "bladerf",
    .open = mock_open,
    .start = mock_start,
    .is_streaming = mock_is_streaming,
    .stop = mock_stop,
    .close = mock_close,
};

const sdr_ops_t usrp_sdr_ops = {
    .name = "usrp",
    .open = mock_open,
    .start = mock_start,
    .is_streaming = mock_is_streaming,
    .stop = mock_stop,
    .close = mock_close,
};

static void test_sdr_driver_selection_hackrf(void) {
    sniffer_config_t cfg;
    config_init(&cfg);
    cfg.bladerf_num = -1;
    cfg.usrp_serial = NULL;
    cfg.serial = NULL;

    sdr_dev_t *dev = sdr_open_device(&cfg);
    assert(dev != NULL);
    assert(dev->ops != NULL);
    assert(strcmp(dev->ops->name, "hackrf") == 0);

    assert(sdr_start(dev) == 0);
    assert(sdr_is_streaming(dev) == true);
    assert(sdr_stop(dev) == 0);
    assert(sdr_is_streaming(dev) == false);

    sdr_close(dev);
    config_free(&cfg);
    printf("[PASS] test_sdr_driver_selection_hackrf\n");
}

static void test_sdr_driver_selection_bladerf(void) {
    sniffer_config_t cfg;
    config_init(&cfg);
    cfg.bladerf_num = 0;
    cfg.usrp_serial = NULL;

    sdr_dev_t *dev = sdr_open_device(&cfg);
    assert(dev != NULL);
    assert(dev->ops != NULL);
    assert(strcmp(dev->ops->name, "bladerf") == 0);

    assert(sdr_start(dev) == 0);
    assert(sdr_is_streaming(dev) == true);
    assert(sdr_stop(dev) == 0);

    sdr_close(dev);
    config_free(&cfg);
    printf("[PASS] test_sdr_driver_selection_bladerf\n");
}

static void test_sdr_driver_selection_usrp(void) {
    sniffer_config_t cfg;
    config_init(&cfg);
    cfg.bladerf_num = -1;
    cfg.usrp_serial = strdup("123456");

    sdr_dev_t *dev = sdr_open_device(&cfg);
    assert(dev != NULL);
    assert(dev->ops != NULL);
    assert(strcmp(dev->ops->name, "usrp") == 0);

    assert(sdr_start(dev) == 0);
    assert(sdr_is_streaming(dev) == true);
    assert(sdr_stop(dev) == 0);

    sdr_close(dev);
    config_free(&cfg);
    printf("[PASS] test_sdr_driver_selection_usrp\n");
}

int main(void) {
    printf("===========================================\n");
    printf(" Running SDR HAL Unit Tests                \n");
    printf("===========================================\n");
    test_sdr_driver_selection_hackrf();
    test_sdr_driver_selection_bladerf();
    test_sdr_driver_selection_usrp();
    printf("===========================================\n");
    printf(" All SDR HAL tests passed successfully!    \n");
    printf("===========================================\n");
    return 0;
}
