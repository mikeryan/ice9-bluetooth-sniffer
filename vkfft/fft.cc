/*
 * Copyright 2023 ICE9 Consulting LLC
 */

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

//general parts
#include <stdio.h>
#include <vector>
#include <memory>
#include <string.h>
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

#include "vkFFT.h"

#include "Foundation/Foundation.hpp"
#include "QuartzCore/QuartzCore.hpp"
#include "Metal/Metal.hpp"

extern sig_atomic_t running;

#include "fft.h"

struct VkGPU {
    MTL::Device* device;
    MTL::CommandQueue* queue;
};

struct fft_t {
    VkGPU gpu;
    VkFFTApplication app;

    MTL::Buffer *buffer;
    uint64_t bufferSize;
    enum buffer_state_t {
        BUFFER_STATE_READY,
        BUFFER_STATE_FILLING,
        BUFFER_STATE_EXECUTING,
        BUFFER_STATE_DONE,
        BUFFER_STATE_EMPTYING,
    } buffer_state;

    pthread_mutex_t mutex;
    pthread_cond_t buffer_state_cond;
};

#define NUM_FFT 2
fft_t fft[NUM_FFT] = {};
unsigned cur_fft = 0;

extern "C" {
    void fft_done(void *, void *);
}

VkFFTResult init_fft(fft_t *f, unsigned width, unsigned batch_size, MTL::Device *device) {
    pthread_cond_init(&f->buffer_state_cond, NULL);
    pthread_mutex_init(&f->mutex, NULL);

    f->buffer_state = fft_t::BUFFER_STATE_READY;

    f->gpu.device = device;
    MTL::CommandQueue* queue = device->newCommandQueue();
    f->gpu.queue = queue;
    VkFFTResult resFFT = VKFFT_SUCCESS;

    VkFFTConfiguration configuration = {};
    configuration.FFTdim = 1;
    configuration.size[0] = width;
    configuration.numberBatches = batch_size;

    configuration.device = f->gpu.device;
    configuration.queue = f->gpu.queue;

    // input data buffer
    f->bufferSize = (uint64_t)sizeof(float) * 2 * width * batch_size;
    f->buffer = f->gpu.device->newBuffer(f->bufferSize, MTL::ResourceStorageModeShared);
    configuration.buffer = &f->buffer;

    configuration.bufferSize = &f->bufferSize;

    // Initialize applications. This function loads shaders, creates pipeline
    // and configures FFT based on configuration file. No buffer allocations
    // inside VkFFT library.
    resFFT = initializeVkFFT(&f->app, configuration);
    return resFFT;
}

VkFFTResult init_fft(unsigned width, unsigned batch_size) {
    NS::Array* devices = MTL::CopyAllDevices();
    MTL::Device* device = (MTL::Device*)devices->object(0);
    VkFFTResult r;

    for (unsigned i = 0; i < NUM_FFT; ++i)
        if ((r = init_fft(&fft[i], width, batch_size, device)) != VKFFT_SUCCESS)
            return r;

    devices->release();

    return r;
}

VkFFTResult submit_fft(void) {
    VkFFTResult resFFT = VKFFT_SUCCESS;
    fft_t *f = &fft[cur_fft];
    VkFFTLaunchParams launchParams = {};

    if (f->buffer_state != fft_t::BUFFER_STATE_FILLING)
        return VKFFT_SUCCESS;

    f->buffer_state = fft_t::BUFFER_STATE_EXECUTING;

    MTL::CommandBuffer* commandBuffer = f->gpu.queue->commandBuffer();
    if (commandBuffer == NULL) return VKFFT_ERROR_FAILED_TO_CREATE_COMMAND_LIST;
    launchParams.commandBuffer = commandBuffer;
    MTL::ComputeCommandEncoder* commandEncoder = commandBuffer->computeCommandEncoder();
    if (commandEncoder == 0) return VKFFT_ERROR_FAILED_TO_CREATE_COMMAND_LIST;
    launchParams.commandEncoder = commandEncoder;
    resFFT = VkFFTAppend(&f->app, 1, &launchParams);
    if (resFFT != VKFFT_SUCCESS) return resFFT;
    commandEncoder->endEncoding();
    commandEncoder->release();

    commandBuffer->addCompletedHandler([f](MTL::CommandBuffer *completedCommandBuffer) {
        f->buffer_state = fft_t::BUFFER_STATE_DONE;
        fft_done(f, f->buffer->contents());
    });

    commandBuffer->commit();
    commandBuffer->release();

    return resFFT;
}

// start FFT of first buffer on GPU
// then ready next buffer
void *get_next_buffer(void) {
    VkFFTResult r = submit_fft();
    if (r != VKFFT_SUCCESS) return NULL;

    cur_fft = (cur_fft + 1) % NUM_FFT;
    fft_t *f = &fft[cur_fft];

    pthread_mutex_lock(&f->mutex);
    while (running && f->buffer_state != fft_t::BUFFER_STATE_READY)
        pthread_cond_wait(&f->buffer_state_cond, &f->mutex);
    if (!running) {
        pthread_mutex_unlock(&f->mutex);
        pthread_exit(NULL);
    }
    f->buffer_state = fft_t::BUFFER_STATE_FILLING;
    pthread_mutex_unlock(&f->mutex);
    return f->buffer->contents();
}

void release_buffer(void *fft_in) {
    fft_t *f = (fft_t *)fft_in;
    pthread_mutex_lock(&f->mutex);
    f->buffer_state = fft_t::BUFFER_STATE_READY;
    pthread_cond_signal(&f->buffer_state_cond);
    pthread_mutex_unlock(&f->mutex);
}
