/*
 * Copyright (c) 2022 ICE9 Consulting LLC
 */

#ifndef __BTBB_H__
#define __BTBB_H__

#include <stdint.h>

void gen_syndrome_map(int bit_errors);
uint32_t btbb_find_ac(char *stream,
           int search_length,
           int max_ac_errors);

#endif /* __BTBB_H__ */
