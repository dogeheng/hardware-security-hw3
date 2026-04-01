#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <x86intrin.h>
#include <string.h>
#include <time.h>

#include "protocol.h"

// Get monotonic time in nanoseconds.
static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [-m message | -f path] [-d bit_ms] [-p packet_bytes] [-r repetitions]\n"
        "  -m message       Message to send (default: HELLO WORLD\\n)\n"
        "  -f path          Read the message from a file\n"
        "  -d bit_ms        Bit duration in milliseconds (default: 200)\n"
        "  -p packet_bytes  Packet payload size in bytes (default: 16, max: 64)\n"
        "  -r repetitions   Packet repetition count (default: 1)\n",
        prog);
}

static int read_file_bytes(const char *path, unsigned char **out_buf, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    unsigned char *buf = NULL;
    size_t cap = 0;
    size_t len = 0;

    if (!fp) {
        perror("[Sender] fopen");
        return -1;
    }

    while (1) {
        if (len == cap) {
            size_t new_cap = (cap == 0) ? 4096 : cap * 2;
            unsigned char *new_buf = realloc(buf, new_cap);
            if (!new_buf) {
                fprintf(stderr, "[Sender] realloc failed while reading %s\n", path);
                free(buf);
                fclose(fp);
                return -1;
            }
            buf = new_buf;
            cap = new_cap;
        }

        size_t n = fread(buf + len, 1, cap - len, fp);
        len += n;

        if (n == 0) {
            if (ferror(fp)) {
                perror("[Sender] fread");
                free(buf);
                fclose(fp);
                return -1;
            }
            break;
        }
    }

    fclose(fp);

    *out_buf = buf;
    *out_len = len;
    return 0;
}

static size_t encode_packet(unsigned char *dst,
                            uint16_t session_id,
                            uint16_t packet_index,
                            uint16_t packet_count,
                            const unsigned char *payload,
                            unsigned char payload_len) {
    unsigned char *header = dst + 2;
    uint16_t crc;

    dst[0] = CHANNEL_SYNC_BYTE0;
    dst[1] = CHANNEL_SYNC_BYTE1;

    write_u16le(header + 0, session_id);
    write_u16le(header + 2, packet_index);
    write_u16le(header + 4, packet_count);
    header[6] = payload_len;

    if (payload_len > 0) {
        memcpy(header + PACKET_HEADER_BYTES, payload, payload_len);
    }

    crc = crc16_ccitt(header, PACKET_HEADER_BYTES + payload_len);
    write_u16le(header + PACKET_HEADER_BYTES + payload_len, crc);

    return PACKET_FIXED_OVERHEAD_BYTES + payload_len;
}

int main(int argc, char *argv[]) {
    const char *default_msg = "HELLO WORLD\n";
    const char *msg_arg = default_msg;
    const char *file_path = NULL;
    unsigned char *message_buf = NULL;
    const unsigned char *message = NULL;
    size_t msg_len = 0;
    uint64_t bit_ms = 200;
    unsigned int packet_payload = DEFAULT_PACKET_PAYLOAD;
    unsigned int repetitions = DEFAULT_PACKET_REPETITIONS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && (i + 1) < argc) {
            msg_arg = argv[++i];
            file_path = NULL;
        } else if (strcmp(argv[i], "-f") == 0 && (i + 1) < argc) {
            file_path = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0 && (i + 1) < argc) {
            bit_ms = strtoull(argv[++i], NULL, 10);
            if (bit_ms == 0) {
                fprintf(stderr, "[Sender] bit duration must be > 0 ms\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-p") == 0 && (i + 1) < argc) {
            packet_payload = (unsigned int)strtoul(argv[++i], NULL, 10);
            if (packet_payload == 0 || packet_payload > MAX_PACKET_PAYLOAD) {
                fprintf(stderr, "[Sender] packet payload must be between 1 and %d bytes\n",
                        MAX_PACKET_PAYLOAD);
                return 1;
            }
        } else if (strcmp(argv[i], "-r") == 0 && (i + 1) < argc) {
            repetitions = (unsigned int)strtoul(argv[++i], NULL, 10);
            if (repetitions == 0) {
                fprintf(stderr, "[Sender] repetitions must be >= 1\n");
                return 1;
            }
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (file_path) {
        if (read_file_bytes(file_path, &message_buf, &msg_len) != 0) {
            return 1;
        }
        message = message_buf;
    } else {
        message = (const unsigned char *)msg_arg;
        msg_len = strlen(msg_arg);
    }

    size_t packet_count_full = (msg_len == 0) ? 1 : ((msg_len + packet_payload - 1) / packet_payload);
    if (packet_count_full > UINT16_MAX) {
        fprintf(stderr, "[Sender] message too long for 16-bit packet numbering\n");
        free(message_buf);
        return 1;
    }
    uint16_t packet_count = (uint16_t)packet_count_full;

    size_t cycle_bytes = 0;
    for (unsigned int rep = 0; rep < repetitions; rep++) {
        for (uint16_t packet_index = 0; packet_index < packet_count; packet_index++) {
            size_t offset = (size_t)packet_index * packet_payload;
            size_t remaining = (offset < msg_len) ? (msg_len - offset) : 0;
            unsigned char payload_len =
                (remaining > packet_payload) ? (unsigned char)packet_payload : (unsigned char)remaining;
            cycle_bytes += PACKET_FIXED_OVERHEAD_BYTES + payload_len;
        }
    }

    unsigned char *cycle = malloc(cycle_bytes);
    if (!cycle) {
        fprintf(stderr, "[Sender] malloc failed while building the packet stream\n");
        free(message_buf);
        return 1;
    }

    uint16_t session_id = crc16_ccitt(message, msg_len);
    size_t cycle_offset = 0;
    for (unsigned int rep = 0; rep < repetitions; rep++) {
        for (uint16_t packet_index = 0; packet_index < packet_count; packet_index++) {
            size_t offset = (size_t)packet_index * packet_payload;
            size_t remaining = (offset < msg_len) ? (msg_len - offset) : 0;
            unsigned char payload_len =
                (remaining > packet_payload) ? (unsigned char)packet_payload : (unsigned char)remaining;

            cycle_offset += encode_packet(cycle + cycle_offset,
                                          session_id,
                                          packet_index,
                                          packet_count,
                                          message + offset,
                                          payload_len);
        }
    }

    printf("[Sender] Starting up...\n");
    if (file_path) {
        printf("[Sender] Source = file:%s\n", file_path);
    } else {
        printf("[Sender] Message = \"%s\"\n", msg_arg);
    }
    printf("[Sender] Message length = %zu bytes\n", msg_len);
    printf("[Sender] Bit duration = %llu ms\n", (unsigned long long)bit_ms);
    printf("[Sender] Packet payload = %u bytes\n", packet_payload);
    printf("[Sender] Packet count = %u\n", packet_count);
    printf("[Sender] Packet repetitions = %u\n", repetitions);
    printf("[Sender] Session ID = 0x%04x\n", session_id);
    fflush(stdout);

    void *handle = dlopen("libc.so.6", RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "[Sender] dlopen failed\n");
        free(cycle);
        free(message_buf);
        return 1;
    }

    void *libc_fn = dlsym(handle, "ecvt_r");
    if (!libc_fn) {
        fprintf(stderr, "[Sender] dlsym failed\n");
        dlclose(handle);
        free(cycle);
        free(message_buf);
        return 1;
    }

    printf("[Sender] libc address = %p\n", libc_fn);
    fflush(stdout);
    dlclose(handle);

    double pi = 3.141592653589793;
    int decpt = 0, sign = 0;
    char buf[64];

    size_t total_bits = cycle_bytes * 8;
    uint64_t bit_ns = bit_ms * 1000000ULL;

    while (1) {
        uint64_t now = now_ns();
        uint64_t slot = now / bit_ns;
        uint64_t slot_end = (slot + 1) * bit_ns;

        size_t bit_index = (size_t)(slot % total_bits);
        size_t byte_index = bit_index / 8;
        int bit_pos = (int)(bit_index % 8);
        int send_one = (cycle[byte_index] >> bit_pos) & 1;

        if (send_one) {
            while (now_ns() < slot_end) {
                int s = ecvt_r(pi, 20, &decpt, &sign, buf, sizeof(buf));
                (void)s;
            }
        } else {
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

    free(cycle);
    free(message_buf);
    return 0;
}
