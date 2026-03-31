#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <x86intrin.h>

#include "../protocol.h"

static inline uint64_t rdtscp64(void) {
    unsigned aux;
    return __rdtscp(&aux);
}

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [-t threshold_cycles] [-d bit_ms] [-n slots] [-m message] [-p packet_bytes] [-r repetitions] [-o csv]\n"
            "  -t threshold_cycles   Timing threshold in cycles (default: 600)\n"
            "  -d bit_ms             Bit duration in milliseconds (default: 5)\n"
            "  -n slots              Number of slots to capture (default: 120)\n"
            "  -m message            Payload used by the sender\n"
            "  -p packet_bytes       Packet payload size in bytes (default: 16)\n"
            "  -r repetitions        Packet repetition count (default: 1)\n"
            "  -o csv                Output CSV path (default: probe_trace.csv)\n",
            prog);
}

static size_t encode_packet(unsigned char *dst,
                            uint16_t session_id,
                            uint16_t packet_index,
                            uint16_t packet_count,
                            unsigned char flags,
                            const unsigned char *payload,
                            unsigned char payload_len) {
    unsigned char *header = dst + 2;
    uint16_t crc;

    dst[0] = CHANNEL_SYNC_BYTE0;
    dst[1] = CHANNEL_SYNC_BYTE1;

    header[0] = make_version_flags(flags);
    write_u16le(header + 1, session_id);
    write_u16le(header + 3, packet_index);
    write_u16le(header + 5, packet_count);
    header[7] = payload_len;

    if (payload_len > 0) {
        memcpy(header + PACKET_HEADER_BYTES, payload, payload_len);
    }

    crc = crc16_ccitt(header, PACKET_HEADER_BYTES + payload_len);
    write_u16le(header + PACKET_HEADER_BYTES + payload_len, crc);

    return PACKET_FIXED_OVERHEAD_BYTES + payload_len;
}

int main(int argc, char *argv[]) {
    uint64_t threshold = 600;
    uint64_t bit_ms = 5;
    size_t slots_to_capture = 120;
    const char *msg = "Packetized covert channels survive duplicates and CRC checks.";
    unsigned int packet_payload = DEFAULT_PACKET_PAYLOAD;
    unsigned int repetitions = DEFAULT_PACKET_REPETITIONS;
    const char *csv_path = "probe_trace.csv";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && (i + 1) < argc) {
            threshold = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-d") == 0 && (i + 1) < argc) {
            bit_ms = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-n") == 0 && (i + 1) < argc) {
            slots_to_capture = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-m") == 0 && (i + 1) < argc) {
            msg = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && (i + 1) < argc) {
            packet_payload = (unsigned int)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-r") == 0 && (i + 1) < argc) {
            repetitions = (unsigned int)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-o") == 0 && (i + 1) < argc) {
            csv_path = argv[++i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (bit_ms == 0 || slots_to_capture == 0 || packet_payload == 0 || packet_payload > MAX_PACKET_PAYLOAD || repetitions == 0) {
        fprintf(stderr, "invalid trace configuration\n");
        return 1;
    }

    const unsigned char *message = (const unsigned char *)msg;
    size_t msg_len = strlen(msg);
    size_t packet_count_full = (msg_len == 0) ? 1 : ((msg_len + packet_payload - 1) / packet_payload);
    if (packet_count_full > UINT16_MAX) {
        fprintf(stderr, "message too long for 16-bit packet numbering\n");
        return 1;
    }
    uint16_t packet_count = (uint16_t)packet_count_full;

    FILE *fp = fopen(csv_path, "w");
    if (!fp) {
        perror("fopen");
        return 1;
    }

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
        fprintf(stderr, "malloc failed\n");
        fclose(fp);
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
            unsigned char flags = 0;

            if (packet_index == 0) {
                flags |= FLAG_START;
            }
            if (packet_index + 1 == packet_count) {
                flags |= FLAG_END;
            }

            cycle_offset += encode_packet(cycle + cycle_offset,
                                          session_id,
                                          packet_index,
                                          packet_count,
                                          flags,
                                          message + offset,
                                          payload_len);
        }
    }

    size_t total_bits = cycle_bytes * 8;
    uint64_t bit_ns = bit_ms * 1000000ULL;

    fprintf(fp, "index,slot_number,avg_cycles,hits,total,hit_ratio,expected_bit,decided_bit\n");

    double pi = 3.141592653589793;
    int decpt = 0, sign = 0;
    char buf[64];

    uint64_t last_slot = (uint64_t)-1;
    size_t captured = 0;

    while (captured < slots_to_capture) {
        uint64_t slot = now_ns() / bit_ns;
        if (slot == last_slot) {
            continue;
        }
        last_slot = slot;

        uint64_t slot_end = (slot + 1) * bit_ns;
        long double sum_cycles = 0.0;
        int hits = 0;
        int total = 0;

        while (now_ns() < slot_end) {
            uint64_t start = rdtscp64();
            int s = ecvt_r(pi, 20, &decpt, &sign, buf, sizeof(buf));
            (void)s;
            uint64_t end = rdtscp64();
            uint64_t t = end - start;

            sum_cycles += (long double)t;
            if (t < threshold) {
                hits++;
            }
            total++;
        }

        double avg_cycles = total > 0 ? (double)(sum_cycles / (long double)total) : 0.0;
        double hit_ratio = total > 0 ? (double)hits / (double)total : 0.0;
        int decided_bit = -1;
        if (hit_ratio > 0.6) {
            decided_bit = 1;
        } else if (hit_ratio < 0.4) {
            decided_bit = 0;
        }

        size_t bit_index = slot % total_bits;
        size_t byte_index = bit_index / 8;
        int bit_pos = bit_index % 8;
        int expected_bit = (cycle[byte_index] >> bit_pos) & 1;

        fprintf(fp, "%zu,%llu,%.3f,%d,%d,%.6f,%d,%d\n",
                captured,
                (unsigned long long)slot,
                avg_cycles,
                hits,
                total,
                hit_ratio,
                expected_bit,
                decided_bit);
        captured++;
    }

    free(cycle);
    fclose(fp);
    return 0;
}
