#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <x86intrin.h>  // for _mm_clflush, __rdtscp
#include <math.h>
#include <time.h>

// Threshold in cycles for distinguishing cache hit vs. cache miss.
// This value must be tuned for the target CPU.
#define THRESHOLD_CYCLES 600ULL

// Read the timestamp counter.
static inline uint64_t rdtscp64() {
    unsigned aux;
    return __rdtscp(&aux);
}

// Get monotonic time in nanoseconds.
static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(int argc, char *argv[]) {
    uint64_t threshold = THRESHOLD_CYCLES;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && (i + 1) < argc) {
            threshold = strtoull(argv[++i], NULL, 10);
        }
    }

    printf("[Receiver] Starting up...\n");
    fflush(stdout);

    double pi = 3.141592653589793;
    int decpt = 0, sign = 0;
    char buf[64];
    uint64_t start, end;

    const uint64_t BIT_NS = 200000000ULL;   // 200 ms per bit
    uint64_t last_slot = (uint64_t)-1;

    uint16_t sync_reg = 0;
    const uint16_t SYNC = 0xA55A;

    int in_frame = 0;
    unsigned char current_byte = 0;
    int bit_count = 0;
    int payload_bytes = 0;
    const int PAYLOAD_LEN = 7;   // "UHELLO\n"

    while (1) {
        uint64_t slot = now_ns() / BIT_NS;
        if (slot == last_slot) {
            continue;
        }
        last_slot = slot;

        int hits = 0;
        int total = 0;
        uint64_t slot_end = (slot + 1) * BIT_NS;

        while (now_ns() < slot_end) {
            start = rdtscp64();
            int s = ecvt_r(pi, 20, &decpt, &sign, buf, sizeof(buf));
            (void)s;
            end = rdtscp64();

            uint64_t t = end - start;
            if (t < threshold) {
                hits++;
            }
            total++;
        }

        double ratio = (total > 0) ? ((double)hits / (double)total) : 0.0;

        int decided_bit = -1;

        if (ratio > 0.6) {
            decided_bit = 1;
        } else if (ratio < 0.4) {
            decided_bit = 0;
        } else {
            decided_bit = -1;
        }

        if (decided_bit < 0) {
            // Uncertain bit: drop current frame state and restart synchronization.
            in_frame = 0;
            sync_reg = 0;
            current_byte = 0;
            bit_count = 0;
            payload_bytes = 0;
            putchar('?');
            fflush(stdout);
            continue;
        }

        if (!in_frame) {
            sync_reg = ((sync_reg << 1) | decided_bit) & 0xFFFF;

            if (sync_reg == SYNC) {
                in_frame = 1;
                current_byte = 0;
                bit_count = 0;
                payload_bytes = 0;
                printf("\n[SYNC]\n");
                fflush(stdout);
            }
        } else {
            current_byte |= (decided_bit << bit_count);
            bit_count++;

            if (bit_count == 8) {
                if (current_byte >= 32 && current_byte <= 126) {
                    putchar(current_byte);
                } else if (current_byte == '\n') {
                    putchar('\n');
                } else {
                    printf("[0x%02x]", current_byte);
                }
                fflush(stdout);

                current_byte = 0;
                bit_count = 0;
                payload_bytes++;

                if (payload_bytes == PAYLOAD_LEN) {
                    in_frame = 0;
                    sync_reg = 0;
                    payload_bytes = 0;
                    printf("[END]\n");
                    fflush(stdout);
                }
            }
        }
    }

    return 0;
}
