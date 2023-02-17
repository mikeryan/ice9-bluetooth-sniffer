/*
 * Copyright 2022 ICE9 Consulting LLC
 */

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <uhd.h>

#include "sdr.h"

extern sig_atomic_t running;
extern pid_t self_pid;
extern unsigned channels;
extern float samp_rate;
extern unsigned center_freq;

const float usrp_gain_val = 60;

#define KVLEN 16
typedef struct _kv_pair_t {
    char key[KVLEN];
    char value[KVLEN];
} kv_pair_t;

kv_pair_t *parse_kv_pairs(char *str, unsigned *pairs_out) {
    char *cur = str, *end = str + strlen(str);
    char *comma = NULL, *equals = NULL;

    unsigned pair = 0;
    kv_pair_t *ret;

    while (cur < end) {
        ++pair;
        if ((comma = strchr(cur, ',')) != NULL)
            cur = comma + 1;
        else
            break;
    }

    ret = calloc(pair, sizeof(kv_pair_t));
    pair = 0;
    cur = str;
    while (cur < end) {
        if ((comma = strchr(cur, ',')) != NULL)
            *comma = 0;
        equals = strchr(cur, '=');
        if (equals == NULL) {
            free(ret);
            return NULL;
        }
        *equals = 0;
        strncpy(ret[pair].key, cur, KVLEN-1);
        ret[pair].key[KVLEN-1] = 0;
        strncpy(ret[pair].value, equals + 1, KVLEN-1);
        ret[pair].value[KVLEN-1] = 0;
        ++pair;
        if (comma == NULL)
            break;
        else
            cur = comma + 1;
    }

    *pairs_out = pair;
    return ret;
}

static char *_find(char *key, kv_pair_t *p, unsigned num_pairs) {
    unsigned i;
    for (i = 0; i < num_pairs; ++i)
        if (strcmp(p[i].key, key) == 0)
            return p[i].value;
    return NULL;
}

void usrp_list(void) {
    char buf[128];
    size_t n;
    kv_pair_t *usrp_info;
    unsigned i, pairs = 0;
    char *product, *type, *serial;
    uhd_string_vector_handle devices_str;

    uhd_string_vector_make(&devices_str);
    uhd_usrp_find("", &devices_str);
    uhd_string_vector_size(devices_str, &n);

    // get type, product name, and serial for each device
    for (i = 0; i < n; ++i) {
        uhd_string_vector_at(devices_str, i, buf, sizeof(buf));
        usrp_info = parse_kv_pairs(buf, &pairs);

        product = _find("product", usrp_info, pairs);
        if (product == NULL)
            product = "unk";

        type = _find("type", usrp_info, pairs);
        serial = _find("serial", usrp_info, pairs);
        if (type == NULL || serial == NULL)
            continue;

        printf("interface {value=usrp-%s-%s}{display=ICE9 Bluetooth%s}\n", product, serial,
                strcmp(type, "b200") == 0 ? "" : " (unsupported)");

        free(usrp_info);
    }

    uhd_string_vector_free(&devices_str);
}

char *usrp_get_serial(char *name) {
    char *dash;
    if (strncmp(name, "usrp-", 5) != 0)
        return NULL;
    dash = strchr(name + 5, '-');
    if (dash == NULL)
        return NULL;
    return dash + 1; // skip the dash
}

uhd_usrp_handle usrp_setup(char *serial) {
    uhd_usrp_handle usrp;
    uhd_error error;
    char arg[128];
    uhd_tune_request_t tune_request = {
        .target_freq = center_freq * 1e6,
        .rf_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
        .dsp_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
    };
    uhd_tune_result_t tune_result;

    snprintf(arg, sizeof(arg), "serial=%s,num_recv_frames=1024", serial);

    error = uhd_usrp_make(&usrp, arg);
    if (error)
        errx(1, "Error opening UHD: %u", error);

    // TODO error handling
    uhd_usrp_set_rx_rate(usrp, samp_rate, 0);
    uhd_usrp_set_rx_gain(usrp, usrp_gain_val, 0, "");
    uhd_usrp_set_rx_freq(usrp, &tune_request, 0, &tune_result);
    char str[128];
    uhd_tune_result_to_pp_string(&tune_result, str, sizeof(str));

    return usrp;
}

void *usrp_stream_thread(void *arg) {
    uhd_usrp_handle usrp = arg;
    uhd_rx_streamer_handle rx_handle;
    uhd_error error;
    uhd_rx_metadata_handle md;
    size_t channel = 0, num_samples, num_rx_samples;
    void *buf;
    uhd_rx_metadata_error_code_t error_code;
    uhd_stream_args_t stream_args = {
        .cpu_format = "fc32",
        .otw_format = "sc16",
        .args = "",
        .channel_list = &channel,
        .n_channels = 1
    };
    uhd_stream_cmd_t stream_cmd = {
        .stream_mode = UHD_STREAM_MODE_START_CONTINUOUS,
        .stream_now = 1,
    };

    uhd_rx_metadata_make(&md);

    uhd_rx_streamer_make(&rx_handle);
    stream_args.channel_list = &channel;
    stream_args.n_channels = 1;
    error = uhd_usrp_get_rx_stream(usrp, &stream_args, rx_handle);
    if (error)
        errx(1, "Error opening RX stream: %u", error);

    uhd_rx_streamer_max_num_samps(rx_handle, &num_samples);
    uhd_rx_streamer_issue_stream_cmd(rx_handle, &stream_cmd);

    while (running) {
        sample_buf_t *s = malloc(sizeof(*s) + num_samples * 2 * sizeof(float));
        buf = s->samples;
        uhd_rx_streamer_recv(rx_handle, &buf, num_samples, &md, 3.0, false, &num_rx_samples);
	uhd_rx_metadata_error_code(md, &error_code);
        if(error_code != UHD_RX_METADATA_ERROR_CODE_NONE && error_code != 8)
            errx(1, "Error during streaming: %u", error_code);
        s->num = num_rx_samples;
        if (running)
            push_samples(s);
        else
            free(s);
    }

    stream_cmd.stream_mode = UHD_STREAM_MODE_STOP_CONTINUOUS;
    uhd_rx_streamer_issue_stream_cmd(rx_handle, &stream_cmd);

    uhd_rx_streamer_free(&rx_handle);
    uhd_rx_metadata_free(&md);

    return NULL;
}

void usrp_close(uhd_usrp_handle usrp) {
    uhd_usrp_free(&usrp);
}
