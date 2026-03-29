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

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [-m message] [-t bit_ms]\n"
        "  -m message   Message to send (default: UHELLO\\n)\n"
        "  -t bit_ms    Bit duration in milliseconds (default: 200)\n",
        prog);
}

int main(int argc, char *argv[]) {
    const char *msg = "HELLO WORLD\n";
    uint64_t bit_ms = 200;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && (i + 1) < argc) {
            msg = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 && (i + 1) < argc) {
            bit_ms = strtoull(argv[++i], NULL, 10);
            if (bit_ms == 0) {
                fprintf(stderr, "[Sender] bit duration must be > 0 ms\n");
                return 1;
            }
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    size_t msg_len = strlen(msg);
    if (msg_len > 255) {
        fprintf(stderr, "[Sender] message too long, max 255 bytes\n");
        return 1;
    }

    printf("[Sender] Starting up...\n");
    printf("[Sender] Message = \"%s\"\n", msg);
    printf("[Sender] Message length = %zu bytes\n", msg_len);
    printf("[Sender] Bit duration = %llu ms\n", (unsigned long long)bit_ms);
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
        dlclose(handle);
        return 1;
    }

    printf("[Sender] libc address = %p\n", libc_fn);
    fflush(stdout);
    dlclose(handle);

    double pi = 3.141592653589793;
    int decpt = 0, sign = 0;
    char buf[64];

    // Frame format:
    // [0xA5][0x5A][LEN][PAYLOAD...]
    size_t frame_len = 2 + 1 + msg_len;
    unsigned char *frame = malloc(frame_len);
    if (!frame) {
        fprintf(stderr, "[Sender] malloc failed\n");
        return 1;
    }

    frame[0] = 0xA5;
    frame[1] = 0x5A;
    frame[2] = (unsigned char)msg_len;
    memcpy(frame + 3, msg, msg_len);

    size_t total_bits = frame_len * 8;
    uint64_t BIT_NS = bit_ms * 1000000ULL;

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

    free(frame);
    return 0;
}
