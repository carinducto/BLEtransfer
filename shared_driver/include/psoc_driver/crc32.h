#ifndef PSOC_CRC32_H
#define PSOC_CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Calculate CRC32 for 24-bit packed samples
 * @param samples Array of 32-bit sign-extended samples
 * @param count Number of samples
 * @return CRC32 value
 */
uint32_t calculate_crc32_samples(const int32_t* samples, size_t count);

/**
 * Calculate CRC32 for raw byte data
 * @param data Pointer to data buffer
 * @param length Length of data in bytes
 * @return CRC32 value
 */
uint32_t calculate_crc32_data(const uint8_t* data, size_t length);

#ifdef __cplusplus
}
#endif

#endif // PSOC_CRC32_H
