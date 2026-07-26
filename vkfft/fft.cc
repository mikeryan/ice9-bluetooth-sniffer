/*
 * Copyright 2023 ICE9 Consulting LLC
 */

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

//general parts
#include <stdio.h>
#include <vector>
#include <memory>
#include <string.h>
#include <signal.h>
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

#if VKFFT_BACKEND == 5
#include "Foundation/Foundation.hpp"
#include "QuartzCore/QuartzCore.hpp"
#include "Metal/Metal.hpp"
#elif VKFFT_BACKEND == 0
#include <vulkan/vulkan.h>
#endif

#include "vkFFT.h"

extern sig_atomic_t running;

#include "fft.h"

#if VKFFT_BACKEND == 5
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

#elif VKFFT_BACKEND == 0

struct VkGPU {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue queue;
    uint32_t queueFamilyIndex;
    VkCommandPool commandPool;
    VkFence fence;
};

struct fft_t {
    VkGPU gpu;
    VkFFTApplication app;

    VkBuffer buffer;
    VkDeviceMemory bufferMemory;
    void* mappedBuffer;
    uint64_t bufferSize;
    VkCommandBuffer commandBuffer;

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

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0xFFFFFFFF;
}

#endif

#define NUM_FFT 2
fft_t fft[NUM_FFT] = {};
unsigned cur_fft = 0;

extern "C" {
    void fft_done(void *, void *);
}

#if VKFFT_BACKEND == 5

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

    commandBuffer->addCompletedHandler([f](MTL::CommandBuffer *completedCommandBuffer) {
        f->buffer_state = fft_t::BUFFER_STATE_DONE;
        fft_done(f, f->buffer->contents());
    });

    commandBuffer->commit();

    return resFFT;
}

#elif VKFFT_BACKEND == 0

static VkGPU global_vulkan_gpu = {};

VkFFTResult init_fft(fft_t *f, unsigned width, unsigned batch_size, const VkGPU &gpu) {
    pthread_cond_init(&f->buffer_state_cond, NULL);
    pthread_mutex_init(&f->mutex, NULL);

    f->buffer_state = fft_t::BUFFER_STATE_READY;
    f->gpu = gpu;

    f->bufferSize = (uint64_t)sizeof(float) * 2 * width * batch_size;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = f->bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(f->gpu.device, &bufferInfo, NULL, &f->buffer) != VK_SUCCESS)
        return VKFFT_ERROR_FAILED_TO_CREATE_BUFFER;

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(f->gpu.device, f->buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(f->gpu.physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(f->gpu.device, &allocInfo, NULL, &f->bufferMemory) != VK_SUCCESS)
        return VKFFT_ERROR_FAILED_TO_ALLOCATE_MEMORY;

    if (vkBindBufferMemory(f->gpu.device, f->buffer, f->bufferMemory, 0) != VK_SUCCESS)
        return VKFFT_ERROR_FAILED_TO_BIND_BUFFER_MEMORY;

    if (vkMapMemory(f->gpu.device, f->bufferMemory, 0, f->bufferSize, 0, &f->mappedBuffer) != VK_SUCCESS)
        return VKFFT_ERROR_FAILED_TO_MAP_MEMORY;

    VkCommandBufferAllocateInfo allocCbInfo = {};
    allocCbInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocCbInfo.commandPool = f->gpu.commandPool;
    allocCbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocCbInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(f->gpu.device, &allocCbInfo, &f->commandBuffer) != VK_SUCCESS)
        return VKFFT_ERROR_FAILED_TO_ALLOCATE_COMMAND_BUFFERS;

    VkFFTConfiguration configuration = {};
    configuration.FFTdim = 1;
    configuration.size[0] = width;
    configuration.numberBatches = batch_size;

    configuration.physicalDevice = &f->gpu.physicalDevice;
    configuration.device = &f->gpu.device;
    configuration.queue = &f->gpu.queue;
    configuration.commandPool = &f->gpu.commandPool;
    configuration.fence = &f->gpu.fence;

    configuration.buffer = &f->buffer;
    configuration.bufferSize = &f->bufferSize;

    return initializeVkFFT(&f->app, configuration);
}

VkFFTResult init_fft(unsigned width, unsigned batch_size) {
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "ice9-bluetooth";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&createInfo, NULL, &global_vulkan_gpu.instance) != VK_SUCCESS)
        return VKFFT_ERROR_FAILED_TO_CREATE_INSTANCE;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(global_vulkan_gpu.instance, &deviceCount, NULL);
    if (deviceCount == 0)
        return VKFFT_ERROR_FAILED_TO_FIND_PHYSICAL_DEVICE;

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(global_vulkan_gpu.instance, &deviceCount, devices.data());
    global_vulkan_gpu.physicalDevice = devices[0];

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(global_vulkan_gpu.physicalDevice, &queueFamilyCount, NULL);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(global_vulkan_gpu.physicalDevice, &queueFamilyCount, queueFamilies.data());

    global_vulkan_gpu.queueFamilyIndex = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            global_vulkan_gpu.queueFamilyIndex = i;
            break;
        }
    }
    if (global_vulkan_gpu.queueFamilyIndex == UINT32_MAX)
        return VKFFT_ERROR_FAILED_TO_FIND_PHYSICAL_DEVICE;

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = global_vulkan_gpu.queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

    if (vkCreateDevice(global_vulkan_gpu.physicalDevice, &deviceCreateInfo, NULL, &global_vulkan_gpu.device) != VK_SUCCESS)
        return VKFFT_ERROR_FAILED_TO_CREATE_DEVICE;

    vkGetDeviceQueue(global_vulkan_gpu.device, global_vulkan_gpu.queueFamilyIndex, 0, &global_vulkan_gpu.queue);

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = global_vulkan_gpu.queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(global_vulkan_gpu.device, &poolInfo, NULL, &global_vulkan_gpu.commandPool) != VK_SUCCESS)
        return VKFFT_ERROR_FAILED_TO_CREATE_COMMAND_POOL;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    if (vkCreateFence(global_vulkan_gpu.device, &fenceInfo, NULL, &global_vulkan_gpu.fence) != VK_SUCCESS)
        return VKFFT_ERROR_FAILED_TO_CREATE_FENCE;

    VkFFTResult r;
    for (unsigned i = 0; i < NUM_FFT; ++i) {
        if ((r = init_fft(&fft[i], width, batch_size, global_vulkan_gpu)) != VKFFT_SUCCESS)
            return r;
    }

    return VKFFT_SUCCESS;
}

VkFFTResult submit_fft(void) {
    fft_t *f = &fft[cur_fft];
    if (f->buffer_state != fft_t::BUFFER_STATE_FILLING)
        return VKFFT_SUCCESS;

    f->buffer_state = fft_t::BUFFER_STATE_EXECUTING;

    vkResetCommandBuffer(f->commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(f->commandBuffer, &beginInfo) != VK_SUCCESS)
        return VKFFT_ERROR_FAILED_TO_BEGIN_COMMAND_BUFFER;

    VkFFTLaunchParams launchParams = {};
    launchParams.commandBuffer = &f->commandBuffer;

    VkFFTResult resFFT = VkFFTAppend(&f->app, 1, &launchParams);
    if (resFFT != VKFFT_SUCCESS) return resFFT;

    if (vkEndCommandBuffer(f->commandBuffer) != VK_SUCCESS)
        return VKFFT_ERROR_FAILED_TO_END_COMMAND_BUFFER;

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &f->commandBuffer;

    vkResetFences(f->gpu.device, 1, &f->gpu.fence);
    if (vkQueueSubmit(f->gpu.queue, 1, &submitInfo, f->gpu.fence) != VK_SUCCESS)
        return VKFFT_ERROR_FAILED_TO_SUBMIT_QUEUE;

    if (vkWaitForFences(f->gpu.device, 1, &f->gpu.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return VKFFT_ERROR_FAILED_TO_WAIT_FOR_FENCES;

    f->buffer_state = fft_t::BUFFER_STATE_DONE;
    fft_done(f, f->mappedBuffer);

    return VKFFT_SUCCESS;
}

#endif

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

#if VKFFT_BACKEND == 5
    return f->buffer->contents();
#elif VKFFT_BACKEND == 0
    return f->mappedBuffer;
#endif
}

void release_buffer(void *fft_in) {
    fft_t *f = (fft_t *)fft_in;
    pthread_mutex_lock(&f->mutex);
    f->buffer_state = fft_t::BUFFER_STATE_READY;
    pthread_cond_signal(&f->buffer_state_cond);
    pthread_mutex_unlock(&f->mutex);
}

void deinit_vkfft(void) {
    for (unsigned i = 0; i < NUM_FFT; ++i) {
        deleteVkFFT(&fft[i].app);
#if VKFFT_BACKEND == 0
        if (fft[i].buffer) {
            vkUnmapMemory(fft[i].gpu.device, fft[i].bufferMemory);
            vkDestroyBuffer(fft[i].gpu.device, fft[i].buffer, NULL);
            vkFreeMemory(fft[i].gpu.device, fft[i].bufferMemory, NULL);
        }
#endif
    }
#if VKFFT_BACKEND == 0
    if (global_vulkan_gpu.device) {
        vkDestroyFence(global_vulkan_gpu.device, global_vulkan_gpu.fence, NULL);
        vkDestroyCommandPool(global_vulkan_gpu.device, global_vulkan_gpu.commandPool, NULL);
        vkDestroyDevice(global_vulkan_gpu.device, NULL);
        vkDestroyInstance(global_vulkan_gpu.instance, NULL);
    }
#endif
}
