#include "psoc_driver/compression.h"
#include <zlib.h>
#include <cstring>

bool decompress_waveform(const uint8_t* compressed_data, size_t compressed_size, int32_t* samples) {
    // Expected size: 2376 samples * 2 bytes (int16 delta-encoded) = 4752 bytes
    const size_t expected_decompressed_size = 2376 * 2;
    uint8_t decompressed_buffer[expected_decompressed_size];

    // Decompress using zlib
    uLongf decompressed_size = expected_decompressed_size;
    int result = uncompress(
        decompressed_buffer,
        &decompressed_size,
        compressed_data,
        compressed_size
    );

    if (result != Z_OK || decompressed_size != expected_decompressed_size) {
        return false;
    }

    // Delta decode: convert 16-bit deltas back to full 24-bit samples
    int32_t prev_sample = 0;
    for (size_t i = 0; i < 2376; i++) {
        // Read 16-bit delta (little-endian)
        int16_t delta;
        memcpy(&delta, &decompressed_buffer[i * 2], sizeof(int16_t));

        // Reconstruct sample
        prev_sample += (int32_t)delta;
        samples[i] = prev_sample;
    }

    return true;
}
