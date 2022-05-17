#ifndef __BLUETOOTH_H__
#define __BLUETOOTH_H__

void bluetooth_detect(uint8_t *bits, unsigned len, unsigned freq, uint32_t *lap_out, uint32_t *aa_out);

#endif
