#include "soapysdr.h"
#include "sdr.h"
#include <SoapySDR/Formats.h>
#include <SoapySDR/Device.h>
#include <complex.h>
#include <err.h>
#include <malloc.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
const int soapysdr_gain_val = 64;
extern sig_atomic_t running;
extern pid_t self_pid;
extern unsigned channels;
extern float samp_rate;
extern unsigned center_freq;
void (*rx_chain)(void*,int,float);

void soapysdr_list(void) {
    size_t soapysdrdevs;
    SoapySDRKwargs* soapysdrdevices;

    soapysdrdevices = SoapySDRDevice_enumerate(NULL, &soapysdrdevs);
    for (size_t j = 0; j < soapysdrdevs; j++)
    {
        char buffer[256]={0};
        snprintf(buffer,255,"interface {value=soapy-%d}{display=ICE9 Bluetooth} => ", (int)j);
        for (size_t k = 0; k < soapysdrdevices[j].size; k++)
        {
            snprintf(buffer+strlen(buffer),255-strlen(buffer), "%s=%s, ", soapysdrdevices[j].keys[k], soapysdrdevices[j].vals[k]);
        }
        snprintf(buffer+strlen(buffer)-2,255-strlen(buffer)+2, "%s", "\n");
        printf("%s",buffer);
    }
    SoapySDRKwargsList_clear(soapysdrdevices, soapysdrdevs);
}


struct SoapySDRDevice *soapysdr_setup(int id)
{
    char identifier[32];
    size_t length=0;
    SoapySDRKwargs *results = SoapySDRDevice_enumerate(NULL, &length);
    if (length==0 || id>(int)length)
    {
        errx(1,"Unable to detect SoapySDR device %d :(",id);
    }
    SoapySDRDevice *sdr = SoapySDRDevice_make(&results[id]);
    if (sdr == NULL)
    {
        errx(1,"SoapySDRDevice_make fail: %s",SoapySDRDevice_lastError());
    }
    snprintf(identifier, sizeof(identifier), "*:instance=%d", id);


    double* samplerates=SoapySDRDevice_listSampleRates(sdr,SOAPY_SDR_RX,0,&length);
    printf("Trying to set a samplerate of %f\n", samp_rate);
    float tmprate=samp_rate;
    for (size_t x=0;x<length;x++)
    {
        double device_samplerate=samplerates[x];
        if (device_samplerate>=samp_rate) {
            tmprate = device_samplerate;
        }
        if (device_samplerate == samp_rate)
        {
            break;
        }
    }
    samp_rate = tmprate;
    printf("Setting a device samplerate of %f\n", samp_rate);
    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, samp_rate) != 0)
    {
        errx(1,"Unable to set soapysdr sample rate: %s", SoapySDRDevice_lastError());
    }

    size_t         bw_length;
    SoapySDRRange* bw_range = SoapySDRDevice_getBandwidthRange(sdr, SOAPY_SDR_RX, 0, &bw_length);
    for (size_t k = 0; k < bw_length; ++k) {
        double bw = samp_rate * 0.75;
        bw        = MIN(bw, bw_range[k].maximum);
        bw        = MAX(bw, bw_range[k].minimum);
        if (SoapySDRDevice_setBandwidth(sdr, SOAPY_SDR_RX, 0, bw) != 0) {
            errx(1,"setBandwidth fail: %s\n", SoapySDRDevice_lastError());
        }
        printf("Set Rx bandwidth to %.2f MHz\n", SoapySDRDevice_getBandwidth(sdr, SOAPY_SDR_RX, 0) / 1e6);
    }

    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, center_freq*1e6, NULL) != 0) {
        errx(1,"Unable to set soapysdr center frequency: %s", SoapySDRDevice_lastError());
    }
    else {
        printf("Setting a frequency of %.2f MHz\n", center_freq*1e6/1e6);
    }

    if (SoapySDRDevice_setGainMode(sdr,SOAPY_SDR_RX,0,0) != 0) {
        errx(1,"Unable to set soapysdr gain mode to non-automatic: %s", SoapySDRDevice_lastError());
    } else {
        printf("Gain mode set to manual.\n");
    }

    SoapySDRRange gain_range = SoapySDRDevice_getGainRange(sdr, SOAPY_SDR_RX, 0);
    int max_gain = gain_range.maximum;
    int min_gain = gain_range.minimum;
    int gain = (int)(((max_gain-min_gain)*0.75)+min_gain);
    printf("Gain range: %d to %d\n",min_gain,max_gain);


    printf("Setting a device gain val of %d\n", gain);
    if (SoapySDRDevice_setGain(sdr,SOAPY_SDR_RX,0,gain) != 0)
    {
        errx(1,"Unable to set soapysdr gain : %s", SoapySDRDevice_lastError());
    }

    return sdr;
}

static int soapy_rx_cs8(const int8_t* buffer, size_t num_samples, float fullScale) {
    sample_buf_t *s = malloc(sizeof(*s) + num_samples * sizeof(int16_t));
    s->num = num_samples;
    memcpy(s->samples,buffer,s->num);
    if (running)
        push_samples(s);
    else
        free(s);
    return 0;
}

static int soapy_rx_cs16(const int16_t* buffer, size_t num_samples, float fullScale) {
    unsigned i;
    sample_buf_t *s = malloc(sizeof(*s) + num_samples * sizeof(int16_t));
    s->num = num_samples;
    for (i = 0; i < s->num*2; ++i) {
        s->samples[i] = buffer[i]/128 + buffer[i+1]/128 * I;
    }
    //printf("%d\n", ((int32_t)buffer[i] + buffer[i+1]) * I)/1024.0f;
    if (running)
        push_samples(s);
    else
        free(s);
    return 0;
}


void *soapysdr_stream_thread(void *arg)
{
    SoapySDRDevice *sdr = arg;
    //setup a stream (complex floats)
    double fullScale;
    char *native_stream_format = SoapySDRDevice_getNativeStreamFormat(sdr,SOAPY_SDR_RX,0,&fullScale);
    int format;
    SoapySDRStream *rxStream = NULL;
    if (strcmp(native_stream_format,SOAPY_SDR_CS16)==0)
    {
        printf("Trying CS16->CS8\n");
        format = 8;
        rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS8, NULL, 0,
                                              NULL);
        if (rxStream == NULL) {
        printf("Using Format CS16.\n");
        format = 16;
        rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, NULL, 0,
                                              NULL);
        }
    }
    else if (strcmp(native_stream_format,SOAPY_SDR_CS8)==0)
    {
        printf("Using Format CS8.\n");
        format = 8;
        rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS8, NULL, 0,
                                              NULL);
    }
    else
    {
        printf("Format not supported.\n");
        return (void*)NULL;
    }
    if (rxStream == NULL)
    {
        printf("setupStream fail: %s\n", SoapySDRDevice_lastError());
        SoapySDRDevice_unmake(sdr);
        return (void*)NULL;
    }
    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0); //start streaming

    //create a re-usable buffer for rx samples

    unsigned samples_per_buffer = channels / 2 * 4096;
    complex float *samples=(uint16_t*)malloc(samples_per_buffer*sizeof(complex float)*2);
    unsigned timeout=100000 * channels / 2 * 4096 / samp_rate;


    if (format==8)
    {
        rx_chain = (void*)soapy_rx_cs8;
    }
    else {
        rx_chain = (void*)soapy_rx_cs16;
    }
    while (running)
    {
        //receive some samples
        void *buffs[] = {samples}; //array of buffers
        int flags; //flags set by receive operation
        long long timeNs; //timestamp for receive buffer
        int ret = SoapySDRDevice_readStream(sdr, rxStream, buffs, samples_per_buffer, &flags, &timeNs, timeout);
        if (ret>0)
        {
            //printf("ret=%d, flags=%d, timeNs=%lld\n", ret, flags, timeNs);
            rx_chain(buffs[0], ret, fullScale);
        }
        //else printf("U");
    }
    free(samples);
    //shutdown the stream
    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0); //stop streaming
    SoapySDRDevice_closeStream(sdr, rxStream);

    //cleanup device handle
    SoapySDRDevice_unmake(sdr);

    printf("Done\n");
    return (void*)NULL;
}
