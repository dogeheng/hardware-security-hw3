#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <x86intrin.h>
#include <string.h>
#include <time.h>

// Get monotonic time in nanoseconds.
static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(void) {
    printf("[Sender] Starting up...\n");
    fflush(stdout);

    // Load the real address from libc.
    void *handle = dlopen("libc.so.6", RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "[Sender] dlopen failed\n");
        return 1;
    }

    // Resolve the shared target function in libc.
    void *libc_fn = dlsym(handle, "ecvt_r");

    if (!libc_fn) {
        fprintf(stderr, "[Sender] dlsym failed\n");
        return 1;
    }

    printf("[Sender] libc address = %p\n", libc_fn);
    fflush(stdout);
    dlclose(handle);

    double pi = 3.141592653589793;
    int decpt = 0, sign = 0;
    char buf[64];

    const unsigned char frame[] = {0xA5, 0x5A, 'U', 'H', 'E', 'L', 'L', 'O', '\n'};
    size_t frame_len = sizeof(frame);
    size_t total_bits = frame_len * 8;
    const uint64_t BIT_NS = 200000000ULL;   // 200 ms per bit

    while (1) {
        uint64_t now = now_ns();
        uint64_t slot = now / BIT_NS;
        uint64_t slot_end = (slot + 1) * BIT_NS;

        size_t bit_index = slot % total_bits;
        size_t byte_index = bit_index / 8;
        int bit_pos = bit_index % 8;
        int send_one = ((frame[byte_index] >> bit_pos) & 1);

        if (send_one) {
            // Send bit 1: keep the target function hot in the cache during the slot.
            while (now_ns() < slot_end) {
                int s = ecvt_r(pi, 20, &decpt, &sign, buf, sizeof(buf));
                (void)s;
            }
        } else {
            // Send bit 0: keep the target function cold by flushing it during the slot.
            while (now_ns() < slot_end) {
                _mm_clflush((char *)libc_fn);
                _mm_clflush((char *)libc_fn + 64);
                _mm_clflush((char *)libc_fn + 128);
                _mm_clflush((char *)libc_fn + 192);
                _mm_clflush((char *)libc_fn + 256);
                _mm_clflush((char *)libc_fn + 320);
                _mm_clflush((char *)libc_fn + 384);
                _mm_clflush((char *)libc_fn + 448);
                _mm_mfence();
            }
        }
    }

    return 0;
}
