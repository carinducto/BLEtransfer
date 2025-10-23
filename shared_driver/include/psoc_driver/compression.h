#ifndef PSOC_COMPRESSION_H
#define PSOC_COMPRESSION_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decompress waveform data using zlib and delta decoding
 * @param compressed_data Pointer to compressed data
 * @param compressed_size Size of compressed data in bytes
 * @param samples Output buffer for decompressed samples (must hold 2376 int32_t values)
 * @return true on success, false on failure
 */
bool decompress_waveform(const uint8_t* compressed_data, size_t compressed_size, int32_t* samples);

#ifdef __cplusplus
}
#endif

#endif // PSOC_COMPRESSION_H
