#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <x86intrin.h>  // for _mm_clflush, __rdtscp
#include <math.h>
#include <time.h>

#define REPORT_INTERVAL 100000  // how often to report stats

// Read timestamp counter
static inline uint64_t rdtscp64() {
    unsigned aux;
    return __rdtscp(&aux);
}

typedef struct {
    uint64_t count;
    double sum;
    long double sum_sq;
} timing_stats;

void update_stats(timing_stats *stats, uint64_t t) {
    stats->count++;
    stats->sum += t;
    stats->sum_sq += (long double)t * t;
}

void print_stats(const char *label, timing_stats *stats) {
    if (stats->count == 0) return;

    double avg = stats->sum / stats->count;
    double variance = (stats->sum_sq / stats->count) - (avg * avg);
    double stddev = sqrt(variance);

    fprintf(stderr, "%s: count=%lu avg=%.2f cycles stddev=%.2f cycles\n",
            label, stats->count, avg, stddev);
    fflush(stderr);
}

int main(void) {
    printf("[Receiver] Starting up...\n");
    fflush(stdout);

    // Get the real address from libc
    void *handle = dlopen("libc.so.6", RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "[Receiver] dlopen failed\n");
        return 1;
    }

    // void *libc_fn = dlsym(handle, "atoi");
    void *libc_fn = dlsym(handle, "ecvt_r");
    if (!libc_fn) {
        fprintf(stderr, "[Receiver] dlsym failed\n");
        return 1;
    }

    printf("[Receiver] libc address = %p\n", libc_fn);
    fflush(stdout);
    dlclose(handle);

    uint64_t iterations = 0;
    timing_stats flushed = {0}, nonflushed = {0};
    srand(time(NULL));

    while (1) {
        int do_flush = rand() % 2;

        _mm_mfence();
        if (do_flush) {
            _mm_clflush((char *)libc_fn);
            _mm_clflush((char *)libc_fn + 64);
            _mm_clflush((char *)libc_fn + 128);
            _mm_clflush((char *)libc_fn + 192);
            _mm_clflush((char *)libc_fn + 256);
            _mm_clflush((char *)libc_fn + 320);
            _mm_clflush((char *)libc_fn + 384);
            _mm_clflush((char *)libc_fn + 448);
        }
        _mm_mfence();

        double pi = 3.141592653589793;
        int decpt = 0, sign = 0;
        char buf[64];

        uint64_t start, end;
        start = rdtscp64();
        int ret = ecvt_r(pi, 20, &decpt, &sign, buf, sizeof(buf));
        (void)ret;
        // int result = atoi("1234567890");
        end = rdtscp64();

        uint64_t t = end - start;

        if (do_flush)
            update_stats(&flushed, t);
        else
            update_stats(&nonflushed, t);

        iterations++;

        if (iterations % REPORT_INTERVAL == 0) {
            fprintf(stderr, "\n--- Stats after %lu iterations ---\n", iterations);
            print_stats("Flushed", &flushed);
            print_stats("Non-Flushed", &nonflushed);
        }
    }

    return 0;
}