#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <complex.h>
#include <vector>
#include <iostream>
#include <cstring>
#include <vulkan/vulkan.h>
#include <fftw3.h>
#include "vkFFT.h"

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

int main(void) {
    const unsigned int seed = 31337;
    srand(seed);

    const int channels = 16;
    const int batch_size = 128;
    const size_t total_complex_elements = (size_t)channels * batch_size;
    const size_t total_float_elements = total_complex_elements * 2;
    const size_t buffer_size_bytes = total_float_elements * sizeof(float);

    std::vector<float> input_data(total_float_elements);
    for (size_t i = 0; i < total_float_elements; i++) {
        input_data[i] = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
    }

    // FFTW Execution
    fftwf_complex *fftw_in = (fftwf_complex *)fftwf_malloc(sizeof(fftwf_complex) * total_complex_elements);
    fftwf_complex *fftw_out = (fftwf_complex *)fftwf_malloc(sizeof(fftwf_complex) * total_complex_elements);

    for (size_t i = 0; i < total_complex_elements; i++) {
        fftw_in[i][0] = input_data[2 * i];
        fftw_in[i][1] = input_data[2 * i + 1];
    }

    int ch = channels;
    fftwf_plan fftw_p = fftwf_plan_many_dft(
        1, &ch, batch_size,
        fftw_in, NULL, 1, channels,
        fftw_out, NULL, 1, channels,
        FFTW_BACKWARD, FFTW_ESTIMATE
    );

    fftwf_execute(fftw_p);

    // VkFFT Execution
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "fft_compare_harness";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo = &appInfo;
    if (vkCreateInstance(&createInfo, NULL, &instance) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan instance\n");
        return 1;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
    if (deviceCount == 0) {
        fprintf(stderr, "No Vulkan physical devices found\n");
        return 1;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    physicalDevice = devices[0];

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    uint32_t computeFamilyIndex = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            computeFamilyIndex = i;
            break;
        }
    }
    if (computeFamilyIndex == UINT32_MAX) {
        fprintf(stderr, "No compute queue family found\n");
        return 1;
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueCreateInfo.queueFamilyIndex = computeFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

    if (vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan device\n");
        return 1;
    }

    vkGetDeviceQueue(device, computeFamilyIndex, 0, &queue);

    VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.queueFamilyIndex = computeFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &poolInfo, NULL, &commandPool) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create command pool\n");
        return 1;
    }

    VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(device, &fenceInfo, NULL, &fence) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create fence\n");
        return 1;
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory bufferMemory = VK_NULL_HANDLE;
    void *mapped_buffer = NULL;

    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = buffer_size_bytes;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &bufferInfo, NULL, &buffer);

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device, &allocInfo, NULL, &bufferMemory);
    vkBindBufferMemory(device, buffer, bufferMemory, 0);
    vkMapMemory(device, bufferMemory, 0, buffer_size_bytes, 0, &mapped_buffer);

    memcpy(mapped_buffer, input_data.data(), buffer_size_bytes);

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo allocCbInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocCbInfo.commandPool = commandPool;
    allocCbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocCbInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &allocCbInfo, &commandBuffer);

    VkFFTConfiguration configuration = {};
    configuration.FFTdim = 1;
    configuration.size[0] = channels;
    configuration.numberBatches = batch_size;
    configuration.physicalDevice = &physicalDevice;
    configuration.device = &device;
    configuration.queue = &queue;
    configuration.commandPool = &commandPool;
    configuration.fence = &fence;
    uint64_t buf_size = buffer_size_bytes;
    configuration.buffer = &buffer;
    configuration.bufferSize = &buf_size;

    VkFFTApplication app = {};
    VkFFTResult res = initializeVkFFT(&app, configuration);
    if (res != VKFFT_SUCCESS) {
        fprintf(stderr, "VkFFT initialization failed: %d\n", res);
        return 1;
    }

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    VkFFTLaunchParams launchParams = {};
    launchParams.commandBuffer = &commandBuffer;
    VkFFTAppend(&app, 1, &launchParams);
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkResetFences(device, 1, &fence);
    vkQueueSubmit(queue, 1, &submitInfo, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    // Compute error metrics
    float *vk_out = (float *)mapped_buffer;
    double sum_sq_diff = 0.0;
    double sum_sq_ref = 0.0;
    float max_abs_diff = 0.0f;

    for (size_t i = 0; i < total_complex_elements; i++) {
        float re_fftw = fftw_out[i][0];
        float im_fftw = fftw_out[i][1];

        float re_vk = vk_out[2 * i];
        float im_vk = vk_out[2 * i + 1];

        float diff_re = re_vk - re_fftw;
        float diff_im = im_vk - im_fftw;

        float abs_diff = sqrtf(diff_re * diff_re + diff_im * diff_im);
        if (abs_diff > max_abs_diff) max_abs_diff = abs_diff;

        sum_sq_diff += (double)(diff_re * diff_re + diff_im * diff_im);
        sum_sq_ref += (double)(re_fftw * re_fftw + im_fftw * im_fftw);
    }

    double rel_rms_error = sqrt(sum_sq_diff / sum_sq_ref);
    const double epsilon = 1e-4;

    printf("=== FFT Validation Test Harness ===\n");
    printf("PRNG Seed:          %u\n", seed);
    printf("FFT Configuration:  1D Batch (width=%d, batches=%d)\n", channels, batch_size);
    printf("Max Absolute Diff:  %.6e\n", max_abs_diff);
    printf("Relative RMS Error: %.6e\n", rel_rms_error);
    printf("Tolerance Epsilon:  %.6e\n", epsilon);

    bool passed = (rel_rms_error < epsilon);
    printf("RESULT:             %s\n", passed ? "PASS" : "FAIL");

    // Teardown
    deleteVkFFT(&app);
    vkUnmapMemory(device, bufferMemory);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, bufferMemory, NULL);
    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, commandPool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    fftwf_destroy_plan(fftw_p);
    fftwf_free(fftw_in);
    fftwf_free(fftw_out);

    return passed ? 0 : 1;
}
