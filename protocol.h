#ifndef HW3_PROTOCOL_H
#define HW3_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define CHANNEL_SYNC_WORD 0xA55A
#define CHANNEL_SYNC_BYTE0 0xA5
#define CHANNEL_SYNC_BYTE1 0x5A

#define DEFAULT_PACKET_PAYLOAD 16
#define MAX_PACKET_PAYLOAD 64
#define DEFAULT_PACKET_REPETITIONS 1

/* After 0xA5 0x5A: session_id(2) + packet_index(2) + packet_count(2) + payload_len(1) */
#define PACKET_HEADER_BYTES 7
#define PACKET_CRC_BYTES 2
#define PACKET_FIXED_OVERHEAD_BYTES (2 + PACKET_HEADER_BYTES + PACKET_CRC_BYTES)

static inline void write_u16le(unsigned char *dst, uint16_t value) {
    dst[0] = (unsigned char)(value & 0xFFu);
    dst[1] = (unsigned char)((value >> 8) & 0xFFu);
}

static inline uint16_t read_u16le(const unsigned char *src) {
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static inline uint16_t crc16_ccitt(const unsigned char *data, size_t len) {
    uint16_t crc = 0xFFFFu;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

#endif
