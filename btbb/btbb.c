/*
 * Copyright 2007 - 2013 Dominic Spill, Michael Ossmann, Will Code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libbtbb; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "btbb.h"
#include "uthash.h"
#include "sw_check_tables.h"

/* maximum number of bit errors in  */
#define MAX_BARKER_ERRORS 1

/* Default access code, used for calculating syndromes */
#define DEFAULT_AC 0xcc7b7268ff614e1bULL

/* lookup table for barker code hamming distance */
static const uint8_t BARKER_DISTANCE[] = {
    3,3,3,2,3,2,2,1,2,3,3,3,3,3,3,2,2,3,3,3,3,3,3,2,1,2,2,3,2,3,3,3,
    3,2,2,1,2,1,1,0,3,3,3,2,3,2,2,1,3,3,3,2,3,2,2,1,2,3,3,3,3,3,3,2,
    2,3,3,3,3,3,3,2,1,2,2,3,2,3,3,3,1,2,2,3,2,3,3,3,0,1,1,2,1,2,2,3,
    3,3,3,2,3,2,2,1,2,3,3,3,3,3,3,2,2,3,3,3,3,3,3,2,1,2,2,3,2,3,3,3};

static const uint64_t barker_correct[] = {
    0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
    0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL,
    0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL,
    0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL};

static const uint64_t pn = 0x83848D96BBCC54FCULL;

typedef struct {
    uint64_t syndrome; /* key */
    uint64_t error;
    UT_hash_handle hh;
} syndrome_struct;

static syndrome_struct *syndrome_map = NULL;

static void add_syndrome(uint64_t syndrome, uint64_t error)
{
    syndrome_struct *s;
    s = malloc(sizeof(syndrome_struct));
    s->syndrome = syndrome;
    s->error = error;

    HASH_ADD(hh, syndrome_map, syndrome, 8, s);
}

static syndrome_struct *find_syndrome(uint64_t syndrome)
{
    syndrome_struct *s;

    HASH_FIND(hh, syndrome_map, &syndrome, 8, s);
    return s;
}

static uint64_t gen_syndrome(uint64_t codeword)
{
    uint64_t syndrome = codeword & 0xffffffff;
    codeword >>= 32;
    syndrome ^= sw_check_table4[codeword & 0xff];
    codeword >>= 8;
    syndrome ^= sw_check_table5[codeword & 0xff];
    codeword >>= 8;
    syndrome ^= sw_check_table6[codeword & 0xff];
    codeword >>= 8;
    syndrome ^= sw_check_table7[codeword & 0xff];
    return syndrome;
}

static void cycle(uint64_t error, int start, int depth, uint64_t codeword)
{
    uint64_t new_error, syndrome, base;
    int i;
    base = 1;
    depth -= 1;
    for (i = start; i < 58; i++)
    {
        new_error = (base << i);
        new_error |= error;
        if (depth)
            cycle(new_error, i + 1, depth, codeword);
        else {
            syndrome = gen_syndrome(codeword ^ new_error);
            add_syndrome(syndrome, new_error);
        }
    }
}

void gen_syndrome_map(int bit_errors)
{
    int i;
    for(i = 1; i <= bit_errors; i++)
        cycle(0, 0, i, DEFAULT_AC);
}

/* Convert some number of bits of an air order array to a host order integer */
static uint8_t air_to_host8(const char *air_order, const int bits)
{
    int i;
    uint8_t host_order = 0;
    for (i = 0; i < bits; i++)
        host_order |= ((uint8_t)air_order[i] << i);
    return host_order;
}

static uint64_t air_to_host64(const char *air_order, const int bits)
{
    int i;
    uint64_t host_order = 0;
    for (i = 0; i < bits; i++)
        host_order |= ((uint64_t)air_order[i] << i);
    return host_order;
}

///* Convert some number of bits in a host order integer to an air order array */
//static void host_to_air(const uint8_t host_order, char *air_order, const int bits)
//{
//    int i;
//    for (i = 0; i < bits; i++)
//        air_order[i] = (host_order >> i) & 0x01;
//}

/* count the number of 1 bits in a uint64_t */
static uint8_t count_bits(uint64_t n)
{
#ifdef __GNUC__
    return (uint8_t) __builtin_popcountll (n);
#else
    uint8_t i = 0;
    for (i = 0; n != 0; i++)
        n &= n - 1;
    return i;
#endif
}

int promiscuous_packet_search(char *stream, int search_length, uint32_t *lap,
                              int max_ac_errors, uint8_t *ac_errors) {
    uint64_t syncword, codeword, syndrome, corrected_barker;
    syndrome_struct *errors;
    char *symbols;
    int count, offset = -1;

    /* Barker code at end of sync word (includes
     * MSB of LAP) is used as a rough filter.
     */
    uint8_t barker = air_to_host8(&stream[57], 6);
    barker <<= 1;

    for (count = 0; count + 64 < search_length; count++) {
        symbols = &stream[count];
        barker >>= 1;
        barker |= (symbols[63] << 6);
        if (BARKER_DISTANCE[barker] <= MAX_BARKER_ERRORS) {
            // Error correction
            syncword = air_to_host64(symbols, 64);

            /* correct the barker code with a simple comparison */
            corrected_barker = barker_correct[(uint8_t)(syncword >> 57)];
            syncword = (syncword & 0x01ffffffffffffffULL) | corrected_barker;

            codeword = syncword ^ pn;

            /* Zero syndrome -> good codeword. */
            syndrome = gen_syndrome(codeword);
            *ac_errors = 0;

            /* Try to fix errors in bad codeword. */
            if (syndrome) {
                errors = find_syndrome(syndrome);
                if (errors != NULL) {
                    syncword ^= errors->error;
                    *ac_errors = count_bits(errors->error);
                    syndrome = 0;
                }
                else {
                    *ac_errors = 0xff;  // fail
                }
            }

            if (*ac_errors <= max_ac_errors) {
                *lap = (syncword >> 34) & 0xffffff;
                offset = count;
                break;
            }
        }
    }
    return offset;
}

/* Looks for an AC in the stream */
uint32_t btbb_find_ac(char *stream, int search_length,
                 int max_ac_errors) {
    int offset;
    uint8_t ac_errors;
    uint32_t lap;

    /* Matching any LAP */
    offset = promiscuous_packet_search(stream, search_length, &lap,
                                       max_ac_errors, &ac_errors);
    if (offset >= 0)
        return lap;

    return 0xffffffff;
}
