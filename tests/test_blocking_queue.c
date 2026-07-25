/*
 * Unit and Multithreaded Stress Tests for blocking_queue.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <assert.h>
#include <pthread.h>

#define C_FEK_BLOCKING_QUEUE_IMPLEMENTATION
#define C_FEK_FAIR_LOCK_IMPLEMENTATION
#include "blocking_queue.h"

// -----------------------------------------------------------------------------
// Test 1: Single-Threaded Deterministic Tests
// -----------------------------------------------------------------------------

static void test_single_thread_bounded_capacity(void) {
    Blocking_Queue bq;
    int res = blocking_queue_init(&bq, 3);
    assert(res == 0);

    void *val;
    // underflow check
    res = blocking_queue_poll(&bq, &val);
    assert(res == BQ_EMPTY);

    // add up to capacity
    assert(blocking_queue_add(&bq, (void*)10) == 0);
    assert(blocking_queue_add(&bq, (void*)20) == 0);
    assert(blocking_queue_add(&bq, (void*)30) == 0);

    // overflow check
    assert(blocking_queue_add(&bq, (void*)40) == BQ_FULL);

    // FIFO poll checks
    assert(blocking_queue_poll(&bq, &val) == 0 && (uintptr_t)val == 10);
    assert(blocking_queue_poll(&bq, &val) == 0 && (uintptr_t)val == 20);
    assert(blocking_queue_poll(&bq, &val) == 0 && (uintptr_t)val == 30);

    // empty again
    assert(blocking_queue_poll(&bq, &val) == BQ_EMPTY);

    blocking_queue_destroy(&bq);
    printf("[PASS] test_single_thread_bounded_capacity\n");
}

static void test_single_thread_boundless_capacity(void) {
    Blocking_Queue bq;
    // capacity <= 0 enables boundless growth
    int res = blocking_queue_init(&bq, 0);
    assert(res == 0);

    // push 1000 items into boundless queue
    for (uintptr_t i = 1; i <= 1000; i++) {
        assert(blocking_queue_add(&bq, (void*)i) == 0);
    }

    // pop and verify all 1000 items in FIFO order
    void *val;
    for (uintptr_t i = 1; i <= 1000; i++) {
        assert(blocking_queue_poll(&bq, &val) == 0);
        assert((uintptr_t)val == i);
    }

    assert(blocking_queue_poll(&bq, &val) == BQ_EMPTY);

    blocking_queue_destroy(&bq);
    printf("[PASS] test_single_thread_boundless_capacity\n");
}

// -----------------------------------------------------------------------------
// Test 2: Multithreaded Producer-Consumer Stress Test
// -----------------------------------------------------------------------------

#define NUM_PRODUCERS 4
#define NUM_CONSUMERS 4
#define ITEMS_PER_PRODUCER 2500
#define TOTAL_ITEMS (NUM_PRODUCERS * ITEMS_PER_PRODUCER)

typedef struct {
    Blocking_Queue *bq;
    int producer_id;
} producer_arg_t;

static atomic_int received_counts[TOTAL_ITEMS + 1];
static atomic_int total_received = 0;

static void *producer_worker(void *arg) {
    producer_arg_t *parg = (producer_arg_t*)arg;
    Blocking_Queue *bq = parg->bq;
    int start_val = parg->producer_id * ITEMS_PER_PRODUCER + 1;
    int end_val = start_val + ITEMS_PER_PRODUCER - 1;

    for (int i = start_val; i <= end_val; i++) {
        int res = blocking_queue_put(bq, (void*)(uintptr_t)i);
        assert(res == 0);
    }
    return NULL;
}

static void *consumer_worker(void *arg) {
    Blocking_Queue *bq = (Blocking_Queue*)arg;

    while (1) {
        void *val = NULL;
        int res = blocking_queue_take(bq, &val);
        if (res == BQ_CLOSED) {
            break;
        }
        assert(res == 0);
        uintptr_t item = (uintptr_t)val;
        assert(item >= 1 && item <= TOTAL_ITEMS);

        // record receipt
        atomic_fetch_add(&received_counts[item], 1);
        int current_total = atomic_fetch_add(&total_received, 1) + 1;

        if (current_total == TOTAL_ITEMS) {
            // close queue once all items have been processed to signal consumers
            blocking_queue_close(bq);
            break;
        }
    }
    return NULL;
}

static void test_multithreaded_producer_consumer_stress(void) {
    Blocking_Queue bq;
    // bounded queue capacity to force thread blocking & unblocking synchronization
    assert(blocking_queue_init(&bq, 16) == 0);

    for (int i = 0; i <= TOTAL_ITEMS; i++) {
        atomic_init(&received_counts[i], 0);
    }
    atomic_init(&total_received, 0);

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumers[NUM_CONSUMERS];
    producer_arg_t pargs[NUM_PRODUCERS];

    // launch consumers first
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_create(&consumers[i], NULL, consumer_worker, &bq);
    }

    // launch producers
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pargs[i].bq = &bq;
        pargs[i].producer_id = i;
        pthread_create(&producers[i], NULL, producer_worker, &pargs[i]);
    }

    // join producers
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }

    // join consumers
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_join(consumers[i], NULL);
    }

    // verify exactly TOTAL_ITEMS received
    assert(atomic_load(&total_received) == TOTAL_ITEMS);

    // verify zero data loss and zero duplicates
    for (int i = 1; i <= TOTAL_ITEMS; i++) {
        assert(atomic_load(&received_counts[i]) == 1);
    }

    blocking_queue_destroy(&bq);
    printf("[PASS] test_multithreaded_producer_consumer_stress (%d items across %d P / %d C)\n",
           TOTAL_ITEMS, NUM_PRODUCERS, NUM_CONSUMERS);
}

// -----------------------------------------------------------------------------
// Test 3: Multithreaded Unblock on Close
// -----------------------------------------------------------------------------

static void *blocked_take_worker(void *arg) {
    Blocking_Queue *bq = (Blocking_Queue*)arg;
    void *val = NULL;
    int res = blocking_queue_take(bq, &val);
    assert(res == BQ_CLOSED);
    return NULL;
}

static void test_multithreaded_close_unblocks_all(void) {
    Blocking_Queue bq;
    assert(blocking_queue_init(&bq, 4) == 0);

    pthread_t workers[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&workers[i], NULL, blocked_take_worker, &bq);
    }

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 50000000 }; // 50ms
    nanosleep(&ts, NULL);

    blocking_queue_close(&bq);

    for (int i = 0; i < 4; i++) {
        pthread_join(workers[i], NULL);
    }

    blocking_queue_destroy(&bq);
    printf("[PASS] test_multithreaded_close_unblocks_all\n");
}

int main(void) {
    printf("===================================================\n");
    printf(" Running Blocking_Queue Unit & Multithreaded Tests \n");
    printf("===================================================\n");
    test_single_thread_bounded_capacity();
    test_single_thread_boundless_capacity();
    test_multithreaded_producer_consumer_stress();
    test_multithreaded_close_unblocks_all();
    printf("===================================================\n");
    printf(" All Blocking_Queue tests passed successfully!     \n");
    printf("===================================================\n");
    return 0;
}
