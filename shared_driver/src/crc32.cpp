#include "psoc_driver/crc32.h"

uint32_t calculate_crc32_samples(const int32_t* samples, size_t count) {
    uint32_t crc = 0xFFFFFFFF;

    // Calculate CRC on packed 24-bit format
    for (size_t i = 0; i < count; i++) {
        int32_t sample = samples[i];
        uint8_t bytes[3] = {
            (uint8_t)(sample & 0xFF),
            (uint8_t)((sample >> 8) & 0xFF),
            (uint8_t)((sample >> 16) & 0xFF)
        };

        for (int j = 0; j < 3; j++) {
            crc ^= bytes[j];
            for (int k = 0; k < 8; k++) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ 0xEDB88320;
                } else {
                    crc >>= 1;
                }
            }
        }
    }

    return ~crc;
}

uint32_t calculate_crc32_data(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}
