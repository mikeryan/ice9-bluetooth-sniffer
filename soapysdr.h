#ifndef ICE9_BLUETOOTH_SNIFFER_SOAPYSDR_H
#define ICE9_BLUETOOTH_SNIFFER_SOAPYSDR_H
#include <SoapySDR/Device.h>

struct SoapySDRDevice *soapysdr_setup(int id);
void soapysdr_list(void);
void *soapysdr_stream_thread(void *arg);

#endif // ICE9_BLUETOOTH_SNIFFER_SOAPYSDR_H
