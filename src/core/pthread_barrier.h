/*
 * Copyright 2022 ICE9 Consulting LLC
 */

#ifndef PTHREAD_BARRIER_H_
#define PTHREAD_BARRIER_H_

#include <pthread.h>
#include <errno.h>

typedef int pthread_barrier_localattr_t;
typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    int tripCount;
    int running;
} pthread_barrier_local_t;


int pthread_barrier_local_init(pthread_barrier_local_t *barrier, const pthread_barrier_localattr_t *attr, unsigned int count)
{
    if(count == 0)
    {
        errno = EINVAL;
        return -1;
    }
    if(pthread_mutex_init(&barrier->mutex, 0) < 0)
    {
        return -1;
    }
    if(pthread_cond_init(&barrier->cond, 0) < 0)
    {
        pthread_mutex_destroy(&barrier->mutex);
        return -1;
    }
    barrier->tripCount = count;
    barrier->count = 0;
    barrier->running = 1;

    return 0;
}

void pthread_barrier_local_shutdown(pthread_barrier_local_t *barrier)
{
    barrier->running = 0;
    pthread_cond_broadcast(&barrier->cond);
}

int pthread_barrier_local_destroy(pthread_barrier_local_t *barrier)
{
    pthread_cond_destroy(&barrier->cond);
    pthread_mutex_destroy(&barrier->mutex);
    return 0;
}

int pthread_barrier_local_wait(pthread_barrier_local_t *barrier)
{
    pthread_mutex_lock(&barrier->mutex);
    ++(barrier->count);
    if(barrier->count >= barrier->tripCount)
    {
        barrier->count = 0;
        pthread_cond_broadcast(&barrier->cond);
        pthread_mutex_unlock(&barrier->mutex);
        if (!barrier->running)
            return -1;
        return 1;
    }
    else
    {
        pthread_cond_wait(&barrier->cond, &(barrier->mutex));
        pthread_mutex_unlock(&barrier->mutex);
        if (!barrier->running)
            return -1;
        return 0;
    }
}

#endif // PTHREAD_BARRIER_H_
