#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include <x86intrin.h>

static inline uint64_t rdtscp64(void) {
    unsigned aux;
    return __rdtscp(&aux);
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [-n samples_per_class] [-o path]\n"
            "  -n samples_per_class  Number of flushed and non-flushed samples (default: 4000)\n"
            "  -o path               Output CSV path (default: threshold_distribution.csv)\n",
            prog);
}

static void flush_target(void *ptr) {
    for (int offset = 0; offset <= 448; offset += 64) {
        _mm_clflush((char *)ptr + offset);
    }
}

int main(int argc, char *argv[]) {
    size_t samples_per_class = 4000;
    const char *out_path = "threshold_distribution.csv";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && (i + 1) < argc) {
            samples_per_class = (size_t)strtoull(argv[++i], NULL, 10);
            if (samples_per_class == 0) {
                fprintf(stderr, "[threshold_distribution] samples_per_class must be > 0\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-o") == 0 && (i + 1) < argc) {
            out_path = argv[++i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    uint32_t *flushed = malloc(samples_per_class * sizeof(*flushed));
    uint32_t *nonflushed = malloc(samples_per_class * sizeof(*nonflushed));
    if (!flushed || !nonflushed) {
        fprintf(stderr, "[threshold_distribution] malloc failed\n");
        free(flushed);
        free(nonflushed);
        return 1;
    }

    void *handle = dlopen("libc.so.6", RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "[threshold_distribution] dlopen failed\n");
        free(flushed);
        free(nonflushed);
        return 1;
    }

    void *libc_fn = dlsym(handle, "ecvt_r");
    if (!libc_fn) {
        fprintf(stderr, "[threshold_distribution] dlsym failed\n");
        dlclose(handle);
        free(flushed);
        free(nonflushed);
        return 1;
    }

    srand((unsigned int)time(NULL));

    double pi = 3.141592653589793;
    int decpt = 0, sign = 0;
    char buf[64];

    size_t flushed_count = 0;
    size_t nonflushed_count = 0;

    while (flushed_count < samples_per_class || nonflushed_count < samples_per_class) {
        int do_flush;
        if (flushed_count >= samples_per_class) {
            do_flush = 0;
        } else if (nonflushed_count >= samples_per_class) {
            do_flush = 1;
        } else {
            do_flush = rand() & 1;
        }

        _mm_mfence();
        if (do_flush) {
            flush_target(libc_fn);
        }
        _mm_mfence();

        uint64_t start = rdtscp64();
        int ret = ecvt_r(pi, 20, &decpt, &sign, buf, sizeof(buf));
        (void)ret;
        uint64_t end = rdtscp64();
        uint32_t cycles = (uint32_t)(end - start);

        if (do_flush) {
            flushed[flushed_count++] = cycles;
        } else {
            nonflushed[nonflushed_count++] = cycles;
        }
    }

    FILE *fp = fopen(out_path, "w");
    if (!fp) {
        perror("[threshold_distribution] fopen");
        dlclose(handle);
        free(flushed);
        free(nonflushed);
        return 1;
    }

    fprintf(fp, "mode,cycles\n");
    for (size_t i = 0; i < samples_per_class; i++) {
        fprintf(fp, "flushed,%u\n", flushed[i]);
    }
    for (size_t i = 0; i < samples_per_class; i++) {
        fprintf(fp, "nonflushed,%u\n", nonflushed[i]);
    }

    fclose(fp);
    dlclose(handle);
    free(flushed);
    free(nonflushed);
    return 0;
}
