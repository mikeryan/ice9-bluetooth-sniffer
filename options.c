/*
 * Copyright 2022 ICE9 Consulting LLC
 */

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <btbb.h>
#include <libhackrf/hackrf.h>

extern FILE *in;
extern char *serial;

extern float samp_rate;
extern unsigned channels;
extern unsigned center_freq;
extern lell_pcap_handle *pcap;
extern int live;
extern int verbose;
extern int stats;

void usage(int exitcode);

static void _print_interfaces(void) {
    int i;
    char *s;
    printf("extcap {version=1.0}\n");
    hackrf_init();
    hackrf_device_list_t *hackrf_devices = hackrf_device_list();
    for (i = 0; i < hackrf_devices->devicecount; ++i) {
        for (s = hackrf_devices->serial_numbers[i]; *s == '0'; ++s)
            ;
        printf("interface {value=hackrf-%s}{display=ICE9 Bluetooth}\n", s);
    }
    hackrf_device_list_free(hackrf_devices);
    exit(0);
}

static void _print_dlts(void) {
    printf("dlt {number=255}{name=LINKTYPE_BLUETOOTH_BREDR_BB}{display=Bluetooth BR/EDR and LE}\n");
    printf("dlt {number=256}{name=DLT_BLUETOOTH_LE_LL_WITH_PHDR}{display=Bluetooth LE}\n");
    exit(0);
}

static void _print_config(void) {
    printf("arg {number=0}{call=--channels}{display=Channels}{tooltip=Number of channels to capture}{type=selector}\n");
    printf("value {arg=0}{value=4}{display=4}{default=false}\n");
    printf("value {arg=0}{value=8}{display=8}{default=false}\n");
    printf("value {arg=0}{value=12}{display=12}{default=false}\n");
    printf("value {arg=0}{value=16}{display=16}{default=false}\n");
    printf("value {arg=0}{value=20}{display=20}{default=true}\n");
    printf("arg {number=1}{call=--center-freq}{display=Center Frequency}{tooltip=Center frequency to capture on}{type=integer}{range=2400,2480}{default=2435}\n");
    exit(0);
}

void parse_options(int argc, char **argv) {
    static int do_interfaces = 0, do_dlts = 0, do_config = 0, do_capture = 0;
    int ch;

    static const struct option longopts[] = {
        /* extcap */
        { "extcap-interfaces",      no_argument,            &do_interfaces,           1 },
        { "extcap-dlts", no_argument, &do_dlts, 1 },
        { "extcap-config", no_argument, &do_config, 1 },
        { "capture", no_argument, NULL, 'l' },
        { "extcap-version", required_argument, NULL, 0 }, // ignore
        { "extcap-interface", required_argument, NULL, 'i' },
        { "fifo", required_argument, NULL, 'w' },

        /* generic */
        { "channels", required_argument, NULL, 'C' },
        { "center-freq", required_argument, NULL, 'c' },
        { "file", required_argument, NULL, 'f' },
        { "help", no_argument, NULL, 'h' },
        { "verbose", no_argument, NULL, 'v' },
        { "stats", no_argument, NULL, 's' },
        { NULL,         0,                      NULL,           0 }
    };

    while ((ch = getopt_long(argc, argv, "li:w:C:c:f:vsh", longopts, NULL)) != -1) {
        switch (ch) {
            case 0:
                // long opt
                break;

            case 'l':
                do_capture = 1;
                break;

            case 'i':
                if (strstr(optarg, "hackrf-") != optarg)
                    errx(1, "invalid interface, must start with \"hackrf-\"");
                serial = strdup(optarg + strlen("hackrf-"));
                break;

            case 'w':
                if (lell_pcap_create_file(optarg, &pcap) < 0)
                    errx(1, "Unable to create PCAP %s", optarg);
                break;

            case 'f':
                in = fopen(optarg, "r");
                if (in == NULL)
                    err(1, "Can't open input file");
                break;

            case 'C':
                channels = atoi(optarg);
                break;

            case 'c':
                center_freq = atoi(optarg);
                break;

            case 'v':
                verbose = 1;
                break;

            case 's':
                stats = 1;
                break;

            case '?':
            case 'h':
            default:
                usage(0);
                break;
        }
    }

    int sum = do_interfaces + do_dlts + do_config + do_capture;
    if (in == NULL) {
        if (sum == 0) usage(0);
        if (sum != 1) errx(1, "only one mode of operation supported at a time");
    } else if (sum != 0) {
        errx(1, "don't mix extcap args with regular args");
    }

    if (do_interfaces)
        _print_interfaces();
    if (do_dlts)
        _print_dlts();
    if (do_config)
        _print_config();

    if (center_freq == 0)
        errx(1, "center freq is required");
    if (center_freq < 2400 || center_freq > 2480)
        errx(1, "invalid center freq");
    if (channels != 4 && channels != 8 && channels != 12 && channels != 16 && channels != 20)
        errx(1, "invalid channels, must be 4, 8, 12, 16, or 20");
    samp_rate = channels * 1e6;
    if (do_capture)
        live = 1;
}
