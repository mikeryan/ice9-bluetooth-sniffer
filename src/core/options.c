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

#include "options.h"
#include "hackrf.h"
#include "bladerf.h"
#include "usrp.h"

void usage(int exitcode);

void config_init(sniffer_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->bladerf_num = -1;
}

void config_free(sniffer_config_t *cfg) {
    if (cfg->serial) {
        free(cfg->serial);
        cfg->serial = NULL;
    }
    if (cfg->usrp_serial) {
        free(cfg->usrp_serial);
        cfg->usrp_serial = NULL;
    }
    if (cfg->pcap) {
        pcap_close(cfg->pcap);
        cfg->pcap = NULL;
    }
    if (cfg->in && cfg->in != stdin) {
        fclose(cfg->in);
        cfg->in = NULL;
    }
}

static void do_mkdir(char *path) {
    if (mkdir(path, 0777) < 0 && errno != EEXIST)
        err(1, "Cannot install, unable to make directory %s", path);
}

static void exe_path(char *out) {
#ifdef __linux__
    readlink("/proc/self/exe", out, PATH_MAX);
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

#ifdef HAVE_UHD
void usrp_list(void);
#endif
static void _print_interfaces(void) {
    printf("extcap {version=1.0}\n");
#ifdef HAVE_HACKRF
    hackrf_list();
#endif
#ifdef HAVE_BLADERF
    bladerf_list();
#endif
#ifdef HAVE_UHD
    usrp_list();
#endif
}

static void _print_dlts(void) {
    printf("dlt {number=255}{name=LINKTYPE_BLUETOOTH_BREDR_BB}{display=Bluetooth BR/EDR and LE}\n");
    printf("dlt {number=256}{name=DLT_BLUETOOTH_LE_LL_WITH_PHDR}{display=Bluetooth LE}\n");
}

static void _print_config(void) {
    unsigned i;
    printf("arg {number=0}{call=--channels}{display=Channels}{tooltip=Number of channels to capture}{type=selector}\n");
    for (i = 4; i < 64; i += 4)
        printf("value {arg=0}{value=%d}{display=%d}{default=falses}\n", i, i);
    printf("value {arg=0}{value=96}{display=96}{default=true}\n");
    printf("arg {number=1}{call=--center-freq}{display=Center Frequency}{tooltip=Center frequency to capture on}{type=integer}{range=2400,2480}{default=2441}\n");
}

int parse_options(int argc, char **argv, sniffer_config_t *cfg) {
    int do_interfaces = 0, do_dlts = 0, do_config = 0, do_capture = 0, do_install = 0;
    int ch;

    optind = 1; // Reset getopt state for re-entrancy
    config_init(cfg);

    static const struct option longopts[] = {
        /* extcap */
        { "extcap-interfaces",      no_argument,            NULL,           1 },
        { "extcap-dlts",            no_argument,            NULL,           2 },
        { "extcap-config",          no_argument,            NULL,           3 },
        { "capture",                no_argument,            NULL,          'l' },
        { "extcap-version",         required_argument,      NULL,           0 }, // ignore
        { "extcap-interface",       required_argument,      NULL,          'i' },
        { "fifo",                   required_argument,      NULL,          'w' },

        /* generic */
        { "all-channels",           no_argument,            NULL,          'a' },
        { "channels",               required_argument,      NULL,          'C' },
        { "center-freq",            required_argument,      NULL,          'c' },
        { "file",                   required_argument,      NULL,          'f' },
        { "help",                   no_argument,            NULL,          'h' },
        { "verbose",                no_argument,            NULL,          'v' },
        { "stats",                  no_argument,            NULL,          's' },
        { "install",                no_argument,            NULL,          'I' },
        { NULL,                     0,                      NULL,           0 }
    };

    while ((ch = getopt_long(argc, argv, "li:w:C:c:f:aIvsh", longopts, NULL)) != -1) {
        switch (ch) {
            case 0:
                break;
            case 1:
                do_interfaces = 1;
                break;
            case 2:
                do_dlts = 1;
                break;
            case 3:
                do_config = 1;
                break;

            case 'l':
                do_capture = 1;
                break;

            case 'i':
                if (strstr(optarg, "hackrf-") == optarg) {
#ifdef HAVE_HACKRF
                    cfg->serial = strdup(optarg + strlen("hackrf-"));
#else
                    fprintf(stderr, "HackRF support not compiled in\n");
                    return -1;
#endif
                } else if (strstr(optarg, "bladerf") == optarg) {
#ifdef HAVE_BLADERF
                    cfg->bladerf_num = atoi(optarg + strlen("bladerf"));
#else
                    fprintf(stderr, "BladeRF support not compiled in\n");
                    return -1;
#endif
                } else if (strstr(optarg, "usrp-") == optarg) {
#ifdef HAVE_UHD
                    cfg->usrp_serial = strdup(usrp_get_serial(optarg));
#else
                    fprintf(stderr, "USRP support not compiled in\n");
                    return -1;
#endif
                } else {
                    fprintf(stderr, "invalid interface name\n");
                    return -1;
                }
                break;

            case 'w':
                if ((cfg->pcap = pcap_open(optarg)) == NULL) {
                    fprintf(stderr, "Unable to create PCAP %s\n", optarg);
                    return -1;
                }
                break;

            case 'f':
                cfg->in = fopen(optarg, "r");
                if (cfg->in == NULL) {
                    fprintf(stderr, "Can't open input file\n");
                    return -1;
                }
                break;

            case 'C':
                cfg->channels = atoi(optarg);
                break;

            case 'c':
                cfg->center_freq = atoi(optarg);
                break;

            case 'a':
                cfg->channels = 96;
                cfg->center_freq = 2441;
                break;

            case 'v':
                cfg->verbose = 1;
                break;

            case 's':
                cfg->stats = 1;
                break;

            case 'I':
                do_install = 1;
                break;

            case '?':
            case 'h':
            default:
                usage(0);
                return 1;
        }
    }

    if (do_install) {
        install();
        return 1;
    }

    int sum = do_interfaces + do_dlts + do_config + do_capture;
    if (cfg->in == NULL) {
        if (sum == 0) {
            usage(0);
            return 1;
        }
        if (sum != 1) {
            fprintf(stderr, "only one mode of operation supported at a time\n");
            return -1;
        }
    } else if (sum != 0) {
        fprintf(stderr, "don't mix extcap args with regular args\n");
        return -1;
    }

    if (do_interfaces) {
        _print_interfaces();
        return 1;
    }
    if (do_dlts) {
        _print_dlts();
        return 1;
    }
    if (do_config) {
        _print_config();
        return 1;
    }

    if (cfg->center_freq == 0) {
        fprintf(stderr, "center freq is required\n");
        return -1;
    }
    if (cfg->center_freq < 2400 || cfg->center_freq > 2480) {
        fprintf(stderr, "invalid center freq\n");
        return -1;
    }
    if (cfg->channels < 4 || cfg->channels > 96 || (cfg->channels % 4) != 0) {
        fprintf(stderr, "invalid channels, must be between 4 and 96 and divisible by 4\n");
        return -1;
    }
    cfg->samp_rate = cfg->channels * 1e6f;
    if (do_capture)
        cfg->live = 1;

    return 0;
}
