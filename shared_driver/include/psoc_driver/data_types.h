#ifndef PSOC_DATA_TYPES_H
#define PSOC_DATA_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Waveform header structure (matches PSoC firmware layout)
typedef struct __attribute__((packed)) {
    uint32_t block_number;
    uint32_t timestamp_ms;
    uint32_t sample_rate_hz;
    uint16_t sample_count;
    uint16_t reserved1;           // padding/alignment
    uint16_t trigger_sample;
    uint32_t pulse_freq_hz;
    uint32_t reserved2;           // padding/alignment
    int16_t  temperature_cx10;
    uint8_t  gain_db;
    uint8_t  reserved3;           // padding/alignment
    uint32_t crc32;
    uint16_t reserved4;           // padding to 38 bytes
} waveform_header_t;

// Chunk header structure (for BLE transfer)
typedef struct __attribute__((packed)) {
    uint16_t block_number;
    uint16_t chunk_number;
    uint16_t chunk_size;
    uint16_t total_chunks;
    uint32_t reserved;            // future use
} chunk_header_t;

// Waveform data (header + samples)
typedef struct {
    waveform_header_t header;
    int32_t samples[2376];        // 24-bit samples sign-extended to 32-bit
} waveform_data_t;

// Transfer statistics
typedef struct {
    uint32_t blocks_received;
    uint32_t total_blocks;
    uint32_t total_bytes_received;
    uint32_t total_chunks_received;
    double   throughput_kbps;
    double   progress_percent;
    double   elapsed_seconds;
} transfer_stats_t;

#ifdef __cplusplus
}
#endif

#endif // PSOC_DATA_TYPES_H
