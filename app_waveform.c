/*******************************************************************************
 * File Name: app_waveform.c
 *
 * Description: Ultrasound waveform generation and compression implementation
 *
 *******************************************************************************/

#include "app_waveform.h"
#include "compressed_waveform_data.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/*******************************************************************************
 * Constants
 *******************************************************************************/
#define PI 3.14159265358979323846f

/* Waveform simulation parameters */
#define BASELINE_NOISE_AMPLITUDE   100        /* ±100 counts noise floor (out of ±8M range) */
#define FIRST_ECHO_AMPLITUDE       2500000    /* ~30% of full scale (24-bit signed) */
#define SECOND_ECHO_AMPLITUDE      5000000    /* ~60% of full scale */
#define THIRD_ECHO_AMPLITUDE       1600000    /* ~20% of full scale */

/* Timing parameters (in samples at 50 MHz) */
#define TRIGGER_TIME_SAMPLES       250        /* 5 μs */
#define FIRST_ECHO_TIME_SAMPLES    375        /* 7.5 μs */
#define SECOND_ECHO_TIME_SAMPLES   875        /* 17.5 μs */
#define THIRD_ECHO_TIME_SAMPLES    1250       /* 25 μs */

/* Echo characteristics */
#define ECHO_DURATION_SAMPLES      100        /* ~2 μs echo duration */
#define ECHO_DECAY_RATE            0.03f      /* Exponential decay per sample */

/*******************************************************************************
 * Private Function Prototypes
 *******************************************************************************/
static int32_t generate_baseline_noise(uint32_t sample_index);
static int32_t generate_echo(uint32_t sample_index, uint32_t echo_center,
                             int32_t amplitude, float decay_rate);
static void pack_24bit_sample(int32_t sample, uint8_t *buffer, uint32_t index);
static int32_t unpack_24bit_sample(const uint8_t *buffer, uint32_t index);

/*******************************************************************************
 * Static Variables
 *******************************************************************************/
static uint32_t random_seed = 12345;

/*******************************************************************************
 * Public Functions
 *******************************************************************************/

/**
 * Initialize waveform generation subsystem
 */
void app_waveform_init(void)
{
    /* Initialize random seed for noise generation */
    random_seed = 12345;  /* Could use timer or other entropy source */
}

/**
 * Generate a simulated ultrasound waveform for a given block number
 */
bool app_waveform_generate(uint32_t block_num, waveform_block_header_t *header, uint8_t *samples)
{
    if (header == NULL) {
        return false;
    }

    /* Fill header with metadata */
    memset(header, 0, sizeof(waveform_block_header_t));

    header->block_number = block_num;
    header->timestamp_ms = block_num * 100;  /* Simulate 100ms between captures */
    header->sample_rate_hz = WAVEFORM_SAMPLE_RATE_HZ;
    header->sample_count = WAVEFORM_SAMPLES_PER_BLOCK;
    header->bits_per_sample = WAVEFORM_BITS_PER_SAMPLE;
    header->trigger_sample = TRIGGER_TIME_SAMPLES;
    header->pulse_freq_hz = WAVEFORM_CARRIER_FREQ_HZ;
    header->pulse_cycles = 5;  /* 5-cycle excitation pulse */
    header->pulse_voltage = 200;  /* Normalized to 0-255 */
    header->sensor_id = 1001;  /* Example sensor ID */
    header->temperature_c_x10 = 235;  /* 23.5°C */
    header->gain_db = 60;  /* 60 dB gain */
    header->status_flags = STATUS_FLAG_CALIBRATED | STATUS_FLAG_TEMP_VALID;

    /* If samples buffer is NULL, only return header (for pre-compressed mode) */
    if (samples == NULL) {
        /* Set CRC to pre-compressed reference value */
        header->crc32 = COMPRESSED_WAVEFORM_CRC32;
        return true;
    }

    /* Generate waveform samples */
    for (uint32_t i = 0; i < WAVEFORM_SAMPLES_PER_BLOCK; i++) {
        int32_t sample = 0;

        /* Add baseline noise throughout */
        sample += generate_baseline_noise(i);

        /* Generate echoes at specific time points */
        if (i >= FIRST_ECHO_TIME_SAMPLES && i < FIRST_ECHO_TIME_SAMPLES + ECHO_DURATION_SAMPLES * 3) {
            sample += generate_echo(i, FIRST_ECHO_TIME_SAMPLES, FIRST_ECHO_AMPLITUDE, ECHO_DECAY_RATE);
        }

        if (i >= SECOND_ECHO_TIME_SAMPLES && i < SECOND_ECHO_TIME_SAMPLES + ECHO_DURATION_SAMPLES * 3) {
            sample += generate_echo(i, SECOND_ECHO_TIME_SAMPLES, SECOND_ECHO_AMPLITUDE, ECHO_DECAY_RATE);
        }

        if (i >= THIRD_ECHO_TIME_SAMPLES && i < THIRD_ECHO_TIME_SAMPLES + ECHO_DURATION_SAMPLES * 3) {
            sample += generate_echo(i, THIRD_ECHO_TIME_SAMPLES, THIRD_ECHO_AMPLITUDE, ECHO_DECAY_RATE * 1.5f);
        }

        /* Clamp to 24-bit signed range: -8,388,608 to 8,388,607 */
        if (sample > 8388607) {
            sample = 8388607;
            header->status_flags |= STATUS_FLAG_CLIPPED;
        } else if (sample < -8388608) {
            sample = -8388608;
            header->status_flags |= STATUS_FLAG_CLIPPED;
        }

        /* Pack into 3-byte buffer */
        pack_24bit_sample(sample, samples, i);
    }

    /* Calculate CRC32 for data integrity */
    header->crc32 = app_waveform_crc32(samples, WAVEFORM_RAW_DATA_SIZE);

    return true;
}

/**
 * Compress a waveform block using delta encoding + DEFLATE
 */
bool app_waveform_compress(const waveform_block_header_t *header,
                           const uint8_t *raw_samples,
                           uint8_t *compressed_out,
                           uint32_t *compressed_size_out,
                           uint32_t max_compressed_size)
{
    /* Compression disabled - just copy raw data */
    if (WAVEFORM_RAW_DATA_SIZE > max_compressed_size) {
        return false;
    }

    memcpy(compressed_out, raw_samples, WAVEFORM_RAW_DATA_SIZE);
    *compressed_size_out = WAVEFORM_RAW_DATA_SIZE;

    return true;
}

/**
 * Decompress a waveform block
 */
bool app_waveform_decompress(const uint8_t *compressed_data,
                             uint32_t compressed_size,
                             uint8_t *raw_samples_out)
{
    /* Compression disabled - just copy raw data */
    if (compressed_size != WAVEFORM_RAW_DATA_SIZE) {
        return false;
    }

    memcpy(raw_samples_out, compressed_data, WAVEFORM_RAW_DATA_SIZE);

    return true;
}

/**
 * Calculate CRC32 for sample data
 */
uint32_t app_waveform_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFF;

    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

/*******************************************************************************
 * Private Functions
 *******************************************************************************/

/**
 * Generate baseline noise using simple PRNG
 */
static int32_t generate_baseline_noise(uint32_t sample_index)
{
    /* Simple linear congruential generator for noise */
    random_seed = (random_seed * 1103515245 + 12345) & 0x7FFFFFFF;
    int32_t noise = (int32_t)(random_seed % (BASELINE_NOISE_AMPLITUDE * 2)) - BASELINE_NOISE_AMPLITUDE;
    return noise;
}

/**
 * Generate an echo with 5MHz carrier and exponential decay envelope
 */
static int32_t generate_echo(uint32_t sample_index, uint32_t echo_center,
                             int32_t amplitude, float decay_rate)
{
    int32_t relative_time = (int32_t)sample_index - (int32_t)echo_center;

    /* Exponential decay envelope */
    float envelope = expf(-decay_rate * fabsf((float)relative_time));

    /* 5 MHz carrier wave (10 samples per cycle at 50 MHz sampling) */
    float carrier_phase = 2.0f * PI * WAVEFORM_CARRIER_FREQ_HZ * (float)sample_index / (float)WAVEFORM_SAMPLE_RATE_HZ;
    float carrier = sinf(carrier_phase);

    /* Modulate carrier with envelope */
    int32_t echo = (int32_t)(amplitude * envelope * carrier);

    return echo;
}

/**
 * Pack a 24-bit signed sample into 3-byte buffer (little-endian)
 */
static void pack_24bit_sample(int32_t sample, uint8_t *buffer, uint32_t index)
{
    uint32_t offset = index * 3;
    buffer[offset + 0] = (uint8_t)(sample & 0xFF);
    buffer[offset + 1] = (uint8_t)((sample >> 8) & 0xFF);
    buffer[offset + 2] = (uint8_t)((sample >> 16) & 0xFF);
}

/**
 * Unpack a 24-bit signed sample from 3-byte buffer (little-endian)
 */
static int32_t unpack_24bit_sample(const uint8_t *buffer, uint32_t index)
{
    uint32_t offset = index * 3;
    int32_t sample = (int32_t)buffer[offset + 0] |
                     ((int32_t)buffer[offset + 1] << 8) |
                     ((int32_t)buffer[offset + 2] << 16);

    /* Sign extend from 24-bit to 32-bit */
    if (sample & 0x800000) {
        sample |= 0xFF000000;
    }

    return sample;
}
