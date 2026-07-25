/*
 * Comprehensive unit test suite for btbb.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "btbb/btbb.h"

#define DEFAULT_AC 0xcc7b7268ff614e1bULL
#define PN_SEQ     0x83848D96BBCC54FCULL
#define VALID_SYNCWORD (DEFAULT_AC ^ PN_SEQ)
#define EXPECTED_LAP ((VALID_SYNCWORD >> 34) & 0xffffffULL)

static void uint64_to_air_symbols(uint64_t val, int bits, char *out) {
    for (int i = 0; i < bits; i++) {
        out[i] = (val >> i) & 1;
    }
}

static void test_short_stream(void) {
    char stream[30];
    memset(stream, 0, sizeof(stream));
    uint32_t lap = btbb_find_ac(stream, sizeof(stream), 0);
    assert(lap == 0xffffffff);
    printf("[PASS] test_short_stream\n"); fflush(stdout);
}

static void test_no_match(void) {
    char stream[128];
    memset(stream, 0, sizeof(stream));
    uint32_t lap = btbb_find_ac(stream, sizeof(stream), 0);
    assert(lap == 0xffffffff);
    printf("[PASS] test_no_match\n"); fflush(stdout);
}

static void test_exact_access_code(void) {
    char stream[128];
    memset(stream, 0, sizeof(stream));

    uint64_to_air_symbols(VALID_SYNCWORD, 64, &stream[0]);

    uint32_t lap = btbb_find_ac(stream, sizeof(stream), 0);
    assert(lap == EXPECTED_LAP);
    printf("[PASS] test_exact_access_code (LAP: 0x%06x)\n", lap); fflush(stdout);
}

static void test_access_code_with_offset(void) {
    char stream[200];
    memset(stream, 0, sizeof(stream));
    const int offset = 25;

    uint64_to_air_symbols(VALID_SYNCWORD, 64, &stream[offset]);

    uint32_t lap = btbb_find_ac(stream, sizeof(stream), 0);
    assert(lap == EXPECTED_LAP);
    printf("[PASS] test_access_code_with_offset (Offset: %d, LAP: 0x%06x)\n", offset, lap); fflush(stdout);
}

static void test_single_bit_error_without_syndrome_map(void) {
    char stream[128];
    memset(stream, 0, sizeof(stream));

    // flip bit 12 (in non-barker region)
    uint64_t corrupted_ac = VALID_SYNCWORD ^ (1ULL << 12);
    uint64_to_air_symbols(corrupted_ac, 64, &stream[0]);

    // should fail because syndrome map is not populated for error correction
    uint32_t lap = btbb_find_ac(stream, sizeof(stream), 0);
    assert(lap == 0xffffffff);
    printf("[PASS] test_single_bit_error_without_syndrome_map\n"); fflush(stdout);
}

static void test_single_bit_error_with_syndrome_map(void) {
    char stream[128];
    memset(stream, 0, sizeof(stream));

    // initialize syndrome map for 1-bit error correction
    gen_syndrome_map(1);

    // flip bit 12 (in non-barker region)
    uint64_t corrupted_ac = VALID_SYNCWORD ^ (1ULL << 12);
    uint64_to_air_symbols(corrupted_ac, 64, &stream[0]);

    // should succeed with 1 allowed error
    uint32_t lap = btbb_find_ac(stream, sizeof(stream), 1);
    assert(lap == EXPECTED_LAP);
    printf("[PASS] test_single_bit_error_with_syndrome_map (LAP: 0x%06x)\n", lap); fflush(stdout);
}

static void test_multi_bit_error_exceeds_max_errors(void) {
    char stream[128];
    memset(stream, 0, sizeof(stream));

    // flip bit 12 and bit 14
    uint64_t corrupted_ac = VALID_SYNCWORD ^ (1ULL << 12) ^ (1ULL << 14);
    uint64_to_air_symbols(corrupted_ac, 64, &stream[0]);

    // allow max 1 error, so 2-bit error should fail
    uint32_t lap = btbb_find_ac(stream, sizeof(stream), 1);
    assert(lap == 0xffffffff);
    printf("[PASS] test_multi_bit_error_exceeds_max_errors\n"); fflush(stdout);
}

static void test_multiple_access_codes_in_stream(void) {
    char stream[300];
    memset(stream, 0, sizeof(stream));

    // Place two valid syncwords separated by padding
    uint64_to_air_symbols(VALID_SYNCWORD, 64, &stream[10]);
    uint64_to_air_symbols(VALID_SYNCWORD, 64, &stream[150]);

    uint32_t lap1 = btbb_find_ac(stream, sizeof(stream), 0);
    assert(lap1 == EXPECTED_LAP);

    uint32_t lap2 = btbb_find_ac(stream + 100, sizeof(stream) - 100, 0);
    assert(lap2 == EXPECTED_LAP);

    printf("[PASS] test_multiple_access_codes_in_stream\n"); fflush(stdout);
}

int main(void) {
    printf("======================================\n");
    printf(" Running btbb comprehensive test suite \n");
    printf("======================================\n"); fflush(stdout);
    test_short_stream();
    test_no_match();
    test_exact_access_code();
    test_access_code_with_offset();
    test_single_bit_error_without_syndrome_map();
    test_single_bit_error_with_syndrome_map();
    test_multi_bit_error_exceeds_max_errors();
    test_multiple_access_codes_in_stream();
    printf("======================================\n");
    printf(" All btbb tests passed successfully!  \n");
    printf("======================================\n"); fflush(stdout);
    return 0;
}
