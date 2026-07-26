#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include "fft.h"

sig_atomic_t running = 1;

extern "C" void fft_done(void *fft_in, void *buf_out) {
    (void)fft_in;
    (void)buf_out;
}

int main(void) {
    printf("Testing VkFFT Vulkan initialization...\n");
    VkFFTResult r = init_fft(16, 128);
    assert(r == VKFFT_SUCCESS);
    printf("VkFFT initialized successfully!\n");

    void *buf = get_next_buffer();
    assert(buf != NULL);
    printf("Acquired Vulkan buffer: %p\n", buf);
    release_buffer(buf);

    deinit_vkfft();
    printf("VkFFT deinitialized successfully!\n");
    return 0;
}
