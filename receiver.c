#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <x86intrin.h>
#include <time.h>

#include "protocol.h"

#define THRESHOLD_CYCLES 600ULL

typedef enum {
    SEEK_SYNC = 0,
    READ_HEADER,
    READ_PAYLOAD,
    READ_CRC
} parse_state_t;

typedef struct {
    int active;
    uint16_t session_id;
    uint16_t packet_count;
    unsigned int stored_packets;
    int saw_start;
    int saw_end;
    int announced_complete;
    unsigned char *received;
    unsigned char *payload_lens;
    unsigned char *payloads;
} reassembly_state_t;

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
        "Usage: %s [-t threshold_cycles] [-d bit_ms]\n"
        "  -t threshold_cycles   Timing threshold in cycles (default: 600)\n"
        "  -d bit_ms             Bit duration in milliseconds (default: 200)\n",
        prog);
}

static void reset_reassembly(reassembly_state_t *state) {
    free(state->received);
    free(state->payload_lens);
    free(state->payloads);
    memset(state, 0, sizeof(*state));
}

static int init_reassembly(reassembly_state_t *state, uint16_t session_id, uint16_t packet_count) {
    size_t payload_bytes;

    if (packet_count == 0) {
        return -1;
    }

    payload_bytes = (size_t)packet_count * MAX_PACKET_PAYLOAD;

    reset_reassembly(state);

    state->received = calloc(packet_count, sizeof(unsigned char));
    state->payload_lens = calloc(packet_count, sizeof(unsigned char));
    state->payloads = calloc(payload_bytes, sizeof(unsigned char));
    if (!state->received || !state->payload_lens || !state->payloads) {
        reset_reassembly(state);
        return -1;
    }

    state->active = 1;
    state->session_id = session_id;
    state->packet_count = packet_count;
    return 0;
}

static void print_payload_byte(unsigned char byte) {
    if (byte >= 32 && byte <= 126) {
        putchar(byte);
    } else if (byte == '\n') {
        putchar('\n');
    } else {
        printf("[0x%02x]", byte);
    }
}

static void maybe_print_complete_message(reassembly_state_t *state) {
    if (!state->active || state->announced_complete) {
        return;
    }
    if (!state->saw_start || !state->saw_end || state->stored_packets != state->packet_count) {
        return;
    }

    size_t total_len = 0;
    for (uint16_t i = 0; i < state->packet_count; i++) {
        total_len += state->payload_lens[i];
    }

    printf("[MESSAGE COMPLETE session=0x%04x packets=%u len=%zu]\n",
           state->session_id,
           state->packet_count,
           total_len);

    for (uint16_t i = 0; i < state->packet_count; i++) {
        const unsigned char *payload = state->payloads + (size_t)i * MAX_PACKET_PAYLOAD;
        for (unsigned char j = 0; j < state->payload_lens[i]; j++) {
            print_payload_byte(payload[j]);
        }
    }

    printf("\n[END MESSAGE]\n");
    fflush(stdout);
    state->announced_complete = 1;
}

static void store_packet(reassembly_state_t *state,
                         uint16_t session_id,
                         uint16_t packet_index,
                         uint16_t packet_count,
                         unsigned char flags,
                         const unsigned char *payload,
                         unsigned char payload_len) {
    if (!state->active || state->session_id != session_id || state->packet_count != packet_count) {
        if (init_reassembly(state, session_id, packet_count) != 0) {
            fprintf(stderr, "[Receiver] reassembly allocation failed\n");
            return;
        }
        printf("[Receiver] Session 0x%04x packet_count=%u\n", session_id, packet_count);
        fflush(stdout);
    }

    if (packet_index >= packet_count) {
        return;
    }

    if (flags & FLAG_START) {
        state->saw_start = 1;
    }
    if (flags & FLAG_END) {
        state->saw_end = 1;
    }

    if (!state->received[packet_index]) {
        unsigned char *dst = state->payloads + (size_t)packet_index * MAX_PACKET_PAYLOAD;
        if (payload_len > 0) {
            memcpy(dst, payload, payload_len);
        }
        state->payload_lens[packet_index] = payload_len;
        state->received[packet_index] = 1;
        state->stored_packets++;

        printf("[Receiver] Packet %u/%u stored flags=0x%02x len=%u progress=%u/%u\n",
               packet_index + 1,
               packet_count,
               flags,
               payload_len,
               state->stored_packets,
               state->packet_count);
        fflush(stdout);
    }

    maybe_print_complete_message(state);
}

static void reset_packet_parser(parse_state_t *state,
                                uint16_t *sync_reg,
                                unsigned char *current_byte,
                                int *bit_count,
                                size_t *byte_index,
                                unsigned char *payload_len) {
    *state = SEEK_SYNC;
    *sync_reg = 0;
    *current_byte = 0;
    *bit_count = 0;
    *byte_index = 0;
    *payload_len = 0;
}

int main(int argc, char *argv[]) {
    uint64_t threshold = THRESHOLD_CYCLES;
    uint64_t bit_ms = 200;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && (i + 1) < argc) {
            threshold = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-d") == 0 && (i + 1) < argc) {
            bit_ms = strtoull(argv[++i], NULL, 10);
            if (bit_ms == 0) {
                fprintf(stderr, "[Receiver] bit duration must be > 0 ms\n");
                return 1;
            }
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    printf("[Receiver] Starting up...\n");
    printf("[Receiver] Threshold = %llu cycles\n", (unsigned long long)threshold);
    printf("[Receiver] Bit duration = %llu ms\n", (unsigned long long)bit_ms);
    fflush(stdout);

    double pi = 3.141592653589793;
    int decpt = 0, sign = 0;
    char buf[64];
    uint64_t start, end;

    uint64_t bit_ns = bit_ms * 1000000ULL;
    uint64_t last_slot = (uint64_t)-1;

    parse_state_t parse_state = SEEK_SYNC;
    uint16_t sync_reg = 0;
    unsigned char header[PACKET_HEADER_BYTES];
    unsigned char payload[MAX_PACKET_PAYLOAD];
    unsigned char crc_bytes[PACKET_CRC_BYTES];
    unsigned char current_byte = 0;
    unsigned char packet_payload_len = 0;
    int bit_count = 0;
    size_t byte_index = 0;

    reassembly_state_t reassembly = {0};

    while (1) {
        uint64_t slot = now_ns() / bit_ns;
        if (slot == last_slot) {
            continue;
        }
        last_slot = slot;

        int hits = 0;
        int total = 0;
        uint64_t slot_end = (slot + 1) * bit_ns;

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
        }

        if (decided_bit < 0) {
            reset_packet_parser(&parse_state, &sync_reg, &current_byte, &bit_count, &byte_index, &packet_payload_len);
            continue;
        }

        if (parse_state == SEEK_SYNC) {
            sync_reg = (uint16_t)(((sync_reg << 1) | decided_bit) & 0xFFFFu);
            if (sync_reg == CHANNEL_SYNC_WORD) {
                parse_state = READ_HEADER;
                current_byte = 0;
                bit_count = 0;
                byte_index = 0;
            }
            continue;
        }

        current_byte |= (unsigned char)(decided_bit << bit_count);
        bit_count++;
        if (bit_count < 8) {
            continue;
        }

        unsigned char completed = current_byte;
        current_byte = 0;
        bit_count = 0;

        if (parse_state == READ_HEADER) {
            header[byte_index++] = completed;
            if (byte_index == PACKET_HEADER_BYTES) {
                unsigned char version = packet_version(header[0]);
                packet_payload_len = header[7];
                uint16_t packet_count = read_u16le(header + 5);

                if (version != PROTOCOL_VERSION ||
                    packet_payload_len > MAX_PACKET_PAYLOAD ||
                    packet_count == 0) {
                    reset_packet_parser(&parse_state, &sync_reg, &current_byte, &bit_count, &byte_index, &packet_payload_len);
                    continue;
                }

                byte_index = 0;
                parse_state = (packet_payload_len > 0) ? READ_PAYLOAD : READ_CRC;
            }
            continue;
        }

        if (parse_state == READ_PAYLOAD) {
            payload[byte_index++] = completed;
            if (byte_index == packet_payload_len) {
                byte_index = 0;
                parse_state = READ_CRC;
            }
            continue;
        }

        if (parse_state == READ_CRC) {
            crc_bytes[byte_index++] = completed;
            if (byte_index == PACKET_CRC_BYTES) {
                uint16_t received_crc = read_u16le(crc_bytes);
                unsigned char packet_blob[PACKET_HEADER_BYTES + MAX_PACKET_PAYLOAD];
                uint16_t computed_crc;
                uint16_t session_id;
                uint16_t packet_index;
                uint16_t packet_count;
                unsigned char flags;

                memcpy(packet_blob, header, PACKET_HEADER_BYTES);
                if (packet_payload_len > 0) {
                    memcpy(packet_blob + PACKET_HEADER_BYTES, payload, packet_payload_len);
                }
                computed_crc = crc16_ccitt(packet_blob, PACKET_HEADER_BYTES + packet_payload_len);

                if (computed_crc == received_crc) {
                    session_id = read_u16le(header + 1);
                    packet_index = read_u16le(header + 3);
                    packet_count = read_u16le(header + 5);
                    flags = packet_flags(header[0]);

                    if (packet_index < packet_count) {
                        store_packet(&reassembly,
                                     session_id,
                                     packet_index,
                                     packet_count,
                                     flags,
                                     payload,
                                     packet_payload_len);
                    }
                }

                reset_packet_parser(&parse_state, &sync_reg, &current_byte, &bit_count, &byte_index, &packet_payload_len);
            }
        }
    }

    reset_reassembly(&reassembly);
    return 0;
}
