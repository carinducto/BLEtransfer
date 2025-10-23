/*******************************************************************************
 * File Name: app_waveform.h
 *
 * Description: Ultrasound waveform generation and compression for Inductosense
 *              RTC Data Transfer application
 *
 *******************************************************************************/

#ifndef APP_WAVEFORM_H_
#define APP_WAVEFORM_H_

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * Waveform Parameters
 *******************************************************************************/
#define WAVEFORM_SAMPLE_RATE_HZ      50000000   /* 50 MHz sampling rate */
#define WAVEFORM_SAMPLES_PER_BLOCK   2376       /* Samples per 7KB block (with 40-byte header) */
#define WAVEFORM_TIME_WINDOW_US      47.52f     /* Time window in microseconds */
#define WAVEFORM_CARRIER_FREQ_HZ     5000000    /* 5 MHz carrier frequency */
#define WAVEFORM_BITS_PER_SAMPLE     24         /* 24-bit ADC resolution */

/* Block size definitions */
#define WAVEFORM_HEADER_SIZE         40         /* Header size in bytes */
#define WAVEFORM_RAW_DATA_SIZE       (WAVEFORM_SAMPLES_PER_BLOCK * 3)  /* 24-bit samples = 3 bytes each */
#define WAVEFORM_BLOCK_SIZE          (WAVEFORM_HEADER_SIZE + WAVEFORM_RAW_DATA_SIZE)  /* 7128 bytes total */
#define WAVEFORM_MAX_COMPRESSED_SIZE 4096       /* Maximum compressed size (conservative estimate) */

/* Status flags for waveform capture */
#define STATUS_FLAG_CALIBRATED       0x01       /* Sensor has been calibrated */
#define STATUS_FLAG_TEMP_VALID       0x02       /* Temperature reading valid */
#define STATUS_FLAG_GAIN_AUTO        0x04       /* Auto-gain enabled */
#define STATUS_FLAG_CLIPPED          0x08       /* Signal clipping detected */
#define STATUS_FLAG_LOW_SIGNAL       0x10       /* Signal too weak */
#define STATUS_FLAG_ERROR            0x80       /* General error flag */

/*******************************************************************************
 * Waveform Block Header Structure (40 bytes)
 *******************************************************************************/
typedef struct __attribute__((packed)) {
    /* Block identification */
    uint32_t block_number;          /* 0-1799 */
    uint32_t timestamp_ms;          /* RTC timestamp (ms since boot or epoch) */

    /* Waveform capture parameters */
    uint32_t sample_rate_hz;        /* 50000000 (50 MHz) */
    uint16_t sample_count;          /* 2376 samples */
    uint16_t bits_per_sample;       /* 24 */
    uint16_t trigger_sample;        /* Index where ultrasound pulse was transmitted */

    /* Ultrasound pulse configuration */
    uint32_t pulse_freq_hz;         /* 5000000 (5 MHz center frequency) */
    uint8_t  pulse_cycles;          /* Number of cycles in pulse (e.g., 3-10) */
    uint8_t  pulse_voltage;         /* Drive voltage (0-255, scaled) */

    /* Sensor/environmental metadata */
    uint16_t sensor_id;             /* Unique sensor/transducer ID */
    int16_t  temperature_c_x10;     /* Temperature in °C × 10 (e.g., 235 = 23.5°C) */
    uint8_t  gain_db;               /* Receiver gain in dB (e.g., 20-80 dB) */
    uint8_t  status_flags;          /* Bit flags for status */

    /* Data integrity */
    uint32_t crc32;                 /* CRC32 of sample data for validation */

    /* Reserved for future use */
    uint16_t reserved[2];           /* Padding to 40 bytes */
} waveform_block_header_t;

/*******************************************************************************
 * Function Prototypes
 *******************************************************************************/

/**
 * Initialize waveform generation subsystem
 */
void app_waveform_init(void);

/**
 * Generate a simulated ultrasound waveform for a given block number
 *
 * @param block_num Block number (0-1799)
 * @param header Pointer to header structure to fill
 * @param samples Pointer to buffer for 24-bit samples (must be 2376*3 = 7128 bytes)
 * @return true if successful, false on error
 */
bool app_waveform_generate(uint32_t block_num, waveform_block_header_t *header, uint8_t *samples);

/**
 * Compress a waveform block using delta encoding + DEFLATE
 *
 * @param header Waveform header
 * @param raw_samples Raw 24-bit samples (7128 bytes)
 * @param compressed_out Output buffer for compressed data
 * @param compressed_size_out Pointer to store compressed size
 * @param max_compressed_size Maximum size of output buffer
 * @return true if successful, false on error
 */
bool app_waveform_compress(const waveform_block_header_t *header,
                           const uint8_t *raw_samples,
                           uint8_t *compressed_out,
                           uint32_t *compressed_size_out,
                           uint32_t max_compressed_size);

/**
 * Decompress a waveform block
 *
 * @param compressed_data Compressed data buffer
 * @param compressed_size Size of compressed data
 * @param raw_samples_out Output buffer for decompressed 24-bit samples (must be 7128 bytes)
 * @return true if successful, false on error
 */
bool app_waveform_decompress(const uint8_t *compressed_data,
                             uint32_t compressed_size,
                             uint8_t *raw_samples_out);

/**
 * Calculate CRC32 for sample data
 *
 * @param data Data buffer
 * @param length Data length in bytes
 * @return CRC32 value
 */
uint32_t app_waveform_crc32(const uint8_t *data, uint32_t length);

#endif /* APP_WAVEFORM_H_ */
