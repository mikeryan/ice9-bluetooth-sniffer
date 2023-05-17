/*
 * Copyright 2022 ICE9 Consulting LLC
 */

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// for PATH_MAX
#ifdef __linux__
#include <limits.h>
#else
#include <sys/syslimits.h>
#endif

#include <libhackrf/hackrf.h>

#include "hackrf.h"
#include "bladerf.h"
#include "pcap.h"
#include "usrp.h"

extern FILE *in;
extern char *serial;
extern char *usrp_serial;
extern int bladerf_num;

extern float samp_rate;
extern unsigned channels;
extern unsigned center_freq;
extern pcap_t *pcap;
extern int live;
extern int verbose;
extern int stats;

void usage(int exitcode);

static void do_mkdir(char *path) {
    if (mkdir(path, 0777) < 0 && errno != EEXIST)
        err(1, "Cannot install, unable to make directory %s", path);
}

static void exe_path(char *out) {
#ifdef __linux__
    if (readlink("/proc/self/exe", out, PATH_MAX) < 0)
        err(1, "Unable to install (readlink)");
#else
    char tmp[PATH_MAX];
    uint32_t size = PATH_MAX;
    _NSGetExecutablePath(tmp, &size);
    realpath(tmp, out);
#endif
}

static void install(void) {
    char *home = getenv("HOME");
    char path[PATH_MAX];
    char exe[PATH_MAX];

    if (home == NULL)
        errx(1, "Cannot install: $HOME not set");

    snprintf(path, sizeof(path), "%s/.config", home);
    do_mkdir(path);
    snprintf(path, sizeof(path), "%s/.config/wireshark", home);
    do_mkdir(path);
    snprintf(path, sizeof(path), "%s/.config/wireshark/extcap", home);
    do_mkdir(path);

    exe_path(exe);
    snprintf(path, sizeof(path), "%s/.config/wireshark/extcap/ice9-bluetooth", home);
    if (symlink(exe, path) < 0)
        err(1, "Unable to install");

    puts("ICE9 Bluetooth Sniffer successfully installed to user's Wireshark extcap directory");
    exit(0);
}

void usrp_list(void);
static void _print_interfaces(void) {
    printf("extcap {version=1.0}\n");
    hackrf_list();
    bladerf_list();
    usrp_list();
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
    printf("value {arg=0}{value=24}{display=24}{default=false}\n");
    printf("value {arg=0}{value=28}{display=28}{default=false}\n");
    printf("value {arg=0}{value=32}{display=32}{default=false}\n");
    printf("value {arg=0}{value=36}{display=36}{default=false}\n");
    printf("value {arg=0}{value=40}{display=40}{default=false}\n");
    printf("arg {number=1}{call=--center-freq}{display=Center Frequency}{tooltip=Center frequency to capture on}{type=integer}{range=2400,2480}{default=2427}\n");
    exit(0);
}

void parse_options(int argc, char **argv) {
    static int do_interfaces = 0, do_dlts = 0, do_config = 0, do_capture = 0, do_install = 0;
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
        { "all-channels", no_argument, NULL, 'a' },
        { "channels", required_argument, NULL, 'C' },
        { "center-freq", required_argument, NULL, 'c' },
        { "file", required_argument, NULL, 'f' },
        { "help", no_argument, NULL, 'h' },
        { "verbose", no_argument, NULL, 'v' },
        { "stats", no_argument, NULL, 's' },
        { "install", no_argument, NULL, 'I' },
        { NULL,         0,                      NULL,           0 }
    };

    while ((ch = getopt_long(argc, argv, "li:w:C:c:f:aIvsh", longopts, NULL)) != -1) {
        switch (ch) {
            case 0:
                // long opt
                break;

            case 'l':
                do_capture = 1;
                break;

            case 'i':
                if (strstr(optarg, "hackrf-") == optarg)
                    serial = strdup(optarg + strlen("hackrf-"));
                else if (strstr(optarg, "bladerf") == optarg)
                    bladerf_num = atoi(optarg + strlen("bladerf"));
                else if (strstr(optarg, "usrp-") == optarg)
                    usrp_serial = strdup(usrp_get_serial(optarg));
                else
                    errx(1, "invalid interface, must start with \"hackrf-\" or \"bladerf\"");
                break;

            case 'w':
                if ((pcap = pcap_open(optarg)) == NULL)
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

            case 'a':
                channels = 96;
                center_freq = 2441;
                break;

            case 'v':
                verbose = 1;
                break;

            case 's':
                stats = 1;
                break;

            case 'I':
                do_install = 1;
                break;

            case '?':
            case 'h':
            default:
                usage(0);
                break;
        }
    }

    if (do_install)
        install(); // will exit

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
    if (channels < 4 || channels > 96 || (channels % 4) != 0)
        errx(1, "invalid channels, must be between 4 and 96 and divisible by 4");
    samp_rate = channels * 1e6;
    if (do_capture)
        live = 1;
}
