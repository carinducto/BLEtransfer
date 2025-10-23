/*******************************************************************************
 * File Name: app_data_transfer.c
 *
 * Description: This file implements the data transfer service for bulk data
 *              transfer over BLE with robustness and recovery features.
 *
 *******************************************************************************/

#include "app_data_transfer.h"
#include "app_waveform.h"
#include "static_waveform_data.h"
#include "GeneratedSource/cycfg_gatt_db.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_trace.h"
#include "cyhal.h"
#include "stdio.h"
#include <string.h>

/*******************************************************************************
 *        Global Variables
 *******************************************************************************/

/* Transfer state */
static transfer_state_t current_state = TRANSFER_STATE_IDLE;
static uint16_t connection_id = 0;
static bool notifications_enabled = false;

/* MTU and chunk sizing */
static uint16_t negotiated_mtu = 23;        /* Default minimum MTU */
static uint16_t actual_chunk_size = 12;     /* MTU(23) - ATT(3) - header(8) = 12 bytes */
static uint16_t actual_chunks_per_block = CHUNKS_PER_BLOCK;

/* Block and chunk tracking */
static uint16_t current_block = 0;          /* Current block being sent (0-1799) */
static uint16_t current_chunk = 0;          /* Current chunk within block (0-29) */
static uint16_t last_acked_block = 0;       /* Last block acknowledged by phone */
static bool waiting_for_ack = false;

/* Data buffer for current block */
static uint8_t block_data[BLOCK_SIZE_MAX];
static uint32_t current_block_size = BLOCK_SIZE_MAX;  /* Actual size of current block (compressed) */

/* Statistics */
static transfer_stats_t stats;

/* Benchmark mode switching */
#if BENCHMARK_MODE_ENABLED
static transfer_mode_t current_mode = TRANSFER_MODE_UNCOMPRESSED;
static uint32_t mode_switch_time_ms = 0;
static bool mode_switched = false;
static mode_stats_t uncompressed_stats = {0};
static mode_stats_t compressed_stats = {0};
#endif

/* Congestion tracking for adaptive rate control */
static uint16_t consecutive_send_failures = 0;
static uint16_t consecutive_send_successes = 0;
static uint16_t current_delay_ms = 30;  /* Start at INITIAL_DELAY_MS */
static uint32_t last_congestion_report_time = 0;

/* Credit-based flow control to prevent buffer overflow */
#define MAX_NOTIFICATIONS_IN_FLIGHT  2   /* Maximum notifications queued in BLE stack at once */
static volatile int16_t notification_credits = MAX_NOTIFICATIONS_IN_FLIGHT;
static uint32_t notifications_queued = 0;
static uint32_t notifications_transmitted = 0;

/* Adaptive rate control constants - balanced for reliability */
#define MIN_DELAY_MS           15    /* Minimum delay (fastest) - match connection interval */
#define MAX_DELAY_MS           50    /* Maximum delay (slowest) */
#define INITIAL_DELAY_MS       15    /* Start at minimum for max speed */
#define CONGESTION_THRESHOLD   3     /* Consecutive failures before backing off */
#define SUCCESS_THRESHOLD      50    /* Consecutive successes before speeding up (was 150) */
#define BACKOFF_INCREMENT      5     /* Add 5ms on congestion (granular steps) */
#define SPEEDUP_DECREMENT      1     /* Subtract 1ms on success (gradual speedup) */
#define CONGESTION_REPORT_INTERVAL_MS  5000  /* Report congestion max once per 5 sec */

/* Timer for getting timestamps */
extern cyhal_timer_t data_transfer_timer_obj;  /* We'll reuse the existing timer for timing */

/*******************************************************************************
 *        Forward Declarations
 *******************************************************************************/
static uint32_t generate_block_data(uint16_t block_num, uint8_t *buffer);
static bool send_chunk(uint16_t block_num, uint16_t chunk_num);
static void reset_transfer_state(void);
static uint32_t get_time_ms(void);

/*******************************************************************************
 *        Function Implementations
 *******************************************************************************/

/**
 * Initialize the data transfer module
 */
void app_data_transfer_init(void)
{
    /* Initialize waveform generation subsystem */
    app_waveform_init();

    reset_transfer_state();
    printf("Data Transfer Service initialized\n");
    printf("  Flow Control Configuration:\n");
    printf("    Initial delay:   %d ms\n", INITIAL_DELAY_MS);
    printf("    Min delay:       %d ms\n", MIN_DELAY_MS);
    printf("    Max delay:       %d ms\n", MAX_DELAY_MS);
    printf("    Backoff increment: +%d ms (granular)\n", BACKOFF_INCREMENT);
    printf("    Speedup decrement: -%d ms\n", SPEEDUP_DECREMENT);
    printf("    Congestion threshold: %d failures\n", CONGESTION_THRESHOLD);
    printf("    Success threshold:    %d successes\n", SUCCESS_THRESHOLD);
}

/**
 * Set MTU for calculating chunk size
 */
void app_data_transfer_set_mtu(uint16_t mtu)
{
    negotiated_mtu = mtu;

    /* Calculate usable chunk size: MTU - ATT overhead (3) - header (8) */
    actual_chunk_size = mtu - 3 - sizeof(chunk_header_t);

    /* Recalculate chunks per block (using max size for allocation) */
    actual_chunks_per_block = (BLOCK_SIZE_MAX + actual_chunk_size - 1) / actual_chunk_size;

    printf("MTU set to %d bytes\n", mtu);
    printf("  Usable chunk size: %d bytes\n", actual_chunk_size);
    printf("  Chunks per block: %d\n", actual_chunks_per_block);
}

/**
 * Start data transfer
 */
bool app_data_transfer_start(uint16_t conn_id)
{
    if (!notifications_enabled) {
        printf("Cannot start transfer: notifications not enabled\n");
        return false;
    }

    connection_id = conn_id;
    current_state = TRANSFER_STATE_ACTIVE;
    current_block = 0;
    current_chunk = 0;
    last_acked_block = 0;
    waiting_for_ack = false;

    /* Initialize statistics */
    memset(&stats, 0, sizeof(stats));
    stats.start_time_ms = get_time_ms();

#if BENCHMARK_MODE_ENABLED
    /* Initialize benchmark mode */
    current_mode = TRANSFER_MODE_UNCOMPRESSED;
    mode_switched = false;
    mode_switch_time_ms = 0;

    /* Initialize uncompressed mode stats */
    memset(&uncompressed_stats, 0, sizeof(uncompressed_stats));
    uncompressed_stats.mode = TRANSFER_MODE_UNCOMPRESSED;
    uncompressed_stats.start_time_ms = stats.start_time_ms;

    /* Initialize compressed mode stats */
    memset(&compressed_stats, 0, sizeof(compressed_stats));
    compressed_stats.mode = TRANSFER_MODE_COMPRESSED;

    printf("BENCHMARK MODE: Starting in UNCOMPRESSED mode (will switch after %d seconds)\n",
           BENCHMARK_UNCOMPRESSED_DURATION_MS / 1000);
#endif

    /* Generate first block */
    current_block_size = generate_block_data(current_block, block_data);

    printf("\n========================================\n");
    printf("Data Transfer STARTED\n");
    printf("Total blocks: %d\n", TOTAL_BLOCKS);
    printf("Block size: ~%d bytes (compressed)\n", (int)current_block_size);
    printf("Total data: ~%lu MB (uncompressed)\n", (uint32_t)((uint32_t)TOTAL_BLOCKS * BLOCK_SIZE_RAW) / (1024 * 1024));
    printf("========================================\n\n");

    return true;
}

/**
 * Stop data transfer
 */
void app_data_transfer_stop(void)
{
    printf("Data Transfer STOPPED by user\n");
    current_state = TRANSFER_STATE_IDLE;
    app_data_transfer_print_stats();
}

/**
 * Pause data transfer (on disconnection)
 */
void app_data_transfer_pause(void)
{
    if (current_state == TRANSFER_STATE_ACTIVE ||
        current_state == TRANSFER_STATE_WAITING_ACK) {
        current_state = TRANSFER_STATE_PAUSED;
        stats.disconnections++;
        printf("Data Transfer PAUSED (disconnection)\n");
        printf("  Last sent: Block %d, Chunk %d\n", current_block, current_chunk);
        printf("  Last ACK'd: Block %d\n", last_acked_block);
    }
}

/**
 * Resume data transfer (on reconnection)
 */
bool app_data_transfer_resume(uint16_t conn_id)
{
    if (current_state != TRANSFER_STATE_PAUSED) {
        return false;
    }

    if (!notifications_enabled) {
        printf("Cannot resume: notifications not enabled\n");
        return false;
    }

    connection_id = conn_id;

    /* Resume from last acknowledged block + 1 */
    current_block = last_acked_block;
    if (current_block > 0 && current_chunk == 0) {
        /* If we were mid-block, restart that block */
        /* Otherwise continue from next block */
    }
    current_chunk = 0;
    waiting_for_ack = false;

    /* Regenerate current block data */
    current_block_size = generate_block_data(current_block, block_data);

    current_state = TRANSFER_STATE_ACTIVE;

    printf("Data Transfer RESUMED\n");
    printf("  Resuming from Block %d\n", current_block);
    printf("  Blocks remaining: %d\n", TOTAL_BLOCKS - current_block);

    return true;
}

/**
 * Process next chunk - call this from a task/timer
 */
bool app_data_transfer_process_next_chunk(void)
{
    /* Check if we can send */
    if (current_state != TRANSFER_STATE_ACTIVE) {
        return false;
    }

    if (waiting_for_ack) {
        /* Waiting for ACK, don't send more chunks yet */
        return true;
    }

#if BENCHMARK_MODE_ENABLED
    /* Check if it's time to switch modes */
    uint32_t now = get_time_ms();
    uint32_t elapsed = now - stats.start_time_ms;

    if (!mode_switched && elapsed >= BENCHMARK_UNCOMPRESSED_DURATION_MS) {
        /* Time to switch to compressed mode! */
        mode_switched = true;
        mode_switch_time_ms = now;

        /* Finalize uncompressed stats */
        uncompressed_stats.duration_ms = now - uncompressed_stats.start_time_ms;
        uncompressed_stats.bytes_sent = stats.total_bytes;
        uncompressed_stats.blocks_sent = stats.blocks_sent;
        if (uncompressed_stats.duration_ms > 0) {
            uncompressed_stats.throughput_kbps = (float)uncompressed_stats.bytes_sent / (float)uncompressed_stats.duration_ms;
            uncompressed_stats.block_rate = (float)uncompressed_stats.blocks_sent * 1000.0f / (float)uncompressed_stats.duration_ms;
        }

        /* Switch to compressed mode */
        current_mode = TRANSFER_MODE_COMPRESSED;

        /* Initialize compressed stats */
        compressed_stats.start_time_ms = now;

        printf("\n========================================\n");
        printf("BENCHMARK MODE SWITCH!\n");
        printf("========================================\n");
        printf("Switching from UNCOMPRESSED to COMPRESSED mode\n");
        printf("\nUncompressed Mode Results:\n");
        printf("  Duration:    %lu.%03lu seconds\n",
               uncompressed_stats.duration_ms / 1000,
               uncompressed_stats.duration_ms % 1000);
        printf("  Blocks sent: %d\n", uncompressed_stats.blocks_sent);
        printf("  Bytes sent:  %lu (%.2f MB)\n",
               uncompressed_stats.bytes_sent,
               (float)uncompressed_stats.bytes_sent / (1024.0f * 1024.0f));
        printf("  Throughput:  %.2f KB/s\n", uncompressed_stats.throughput_kbps);
        printf("  Block rate:  %.2f blocks/sec\n", uncompressed_stats.block_rate);
        printf("\nContinuing in COMPRESSED mode...\n");
        printf("========================================\n\n");

        /* Regenerate current block with new mode */
        current_block_size = generate_block_data(current_block, block_data);
    }

    /* Track per-mode statistics */
    if (current_mode == TRANSFER_MODE_COMPRESSED) {
        compressed_stats.bytes_sent = stats.total_bytes - uncompressed_stats.bytes_sent;
        compressed_stats.blocks_sent = stats.blocks_sent - uncompressed_stats.blocks_sent;
        uint32_t compressed_duration = now - compressed_stats.start_time_ms;
        if (compressed_duration > 0) {
            compressed_stats.throughput_kbps = (float)compressed_stats.bytes_sent / (float)compressed_duration;
            compressed_stats.block_rate = (float)compressed_stats.blocks_sent * 1000.0f / (float)compressed_duration;
        }
    }
#endif

    /* Check if transfer is complete */
    if (current_block >= TOTAL_BLOCKS) {
        current_state = TRANSFER_STATE_COMPLETE;
        stats.end_time_ms = get_time_ms();

        printf("\n========================================\n");
        printf("Data Transfer COMPLETE!\n");
        printf("========================================\n\n");
        app_data_transfer_print_stats();

        return false;
    }

    /* Try to send current chunk */
    if (!send_chunk(current_block, current_chunk)) {
        /* Send failed - likely congestion. Return true to indicate transfer is still active
         * but don't advance to next chunk. Caller should retry after delay. */
        return true;
    }

    /* Chunk sent successfully */
    stats.total_chunks++;
    stats.total_bytes += actual_chunk_size;  /* Approximate */

    /* Move to next chunk */
    current_chunk++;

    /* Check if block is complete */
    uint16_t chunks_in_current_block = (current_block_size + actual_chunk_size - 1) / actual_chunk_size;
    if (current_chunk >= chunks_in_current_block) {
        current_chunk = 0;
        current_block++;
        stats.blocks_sent++;

        /* Check if we need to wait for ACK */
        if ((current_block % ACK_INTERVAL) == 0 && current_block < TOTAL_BLOCKS) {
            current_state = TRANSFER_STATE_WAITING_ACK;
            waiting_for_ack = true;
            uint32_t now = get_time_ms();
            uint32_t elapsed = now - stats.start_time_ms;
            float rate_kbps = 0.0f;
            if (elapsed > 0) {
                rate_kbps = ((float)stats.total_bytes * 8.0f) / (float)elapsed;
            }
            printf("[%lu ms] Block %d sent. Waiting for ACK (blocks %d-%d) | Rate: %.2f Kbps\n",
                   now,
                   current_block - 1,
                   current_block - ACK_INTERVAL,
                   current_block - 1,
                   rate_kbps);
        } else if ((current_block % 100) == 0) {
            /* Print progress every 100 blocks */
            uint32_t now = get_time_ms();
            uint32_t elapsed = now - stats.start_time_ms;
            float rate_kbps = 0.0f;
            if (elapsed > 0) {
                rate_kbps = ((float)stats.total_bytes * 8.0f) / (float)elapsed;
            }
            printf("[%lu ms] Progress: %d/%d blocks (%.1f%%) | Rate: %.2f Kbps\n",
                   now,
                   current_block, TOTAL_BLOCKS,
                   (float)current_block * 100.0f / TOTAL_BLOCKS,
                   rate_kbps);
        }

        /* Generate next block data */
        if (current_block < TOTAL_BLOCKS) {
            current_block_size = generate_block_data(current_block, block_data);
        }
    }

    return true;
}

/**
 * Handle control characteristic write from phone
 */
void app_data_transfer_control_write_handler(uint16_t conn_id, wiced_bt_gatt_write_req_t *p_write_req)
{
    control_msg_t *msg = (control_msg_t *)p_write_req->p_val;

    if (p_write_req->val_len < sizeof(control_msg_t)) {
        printf("Invalid control message size\n");
        return;
    }

    switch (msg->command) {
        case CTRL_CMD_START:
            printf("Received START command from phone\n");
            app_data_transfer_start(conn_id);
            break;

        case CTRL_CMD_STOP:
            printf("Received STOP command from phone\n");
            app_data_transfer_stop();
            break;

        case CTRL_CMD_ACK:
        {
            uint32_t now = get_time_ms();
            printf("[%lu ms] Received ACK for blocks up to %d\n", now, msg->block_number);

            if (msg->block_number >= last_acked_block) {
                last_acked_block = msg->block_number + 1;  /* Next block to send */

                /* Resume sending if we were waiting */
                if (waiting_for_ack) {
                    waiting_for_ack = false;
                    current_state = TRANSFER_STATE_ACTIVE;
                    printf("[%lu ms] ACK received. Resuming transfer from block %d\n", now, current_block);
                }
            } else {
                printf("[%lu ms] WARNING: Received old ACK (current last_acked=%d)\n", now, last_acked_block);
            }
        }
            break;

        case CTRL_CMD_REQUEST_RESUME:
            printf("Received RESUME REQUEST from phone\n");
            /* Phone is asking where we left off - send response */
            /* This would be implemented if phone needs to query state */
            break;

        default:
            printf("Unknown control command: 0x%02X\n", msg->command);
            break;
    }
}

/**
 * Handle CCCD write for data block characteristic
 */
void app_data_transfer_cccd_write_handler(uint16_t conn_id, bool enabled)
{
    notifications_enabled = enabled;
    connection_id = conn_id;

    printf("Data Block notifications %s\n", enabled ? "ENABLED" : "DISABLED");

    if (!enabled && current_state == TRANSFER_STATE_ACTIVE) {
        /* Notifications disabled during transfer - pause */
        app_data_transfer_pause();
    }
}

/**
 * Get current transfer statistics
 */
const transfer_stats_t* app_data_transfer_get_stats(void)
{
    return &stats;
}

/**
 * Get current transfer state
 */
transfer_state_t app_data_transfer_get_state(void)
{
    return current_state;
}

/**
 * Print transfer statistics to console
 */
void app_data_transfer_print_stats(void)
{
    uint32_t elapsed_ms = stats.end_time_ms - stats.start_time_ms;
    uint32_t elapsed_sec = elapsed_ms / 1000;
    float throughput_kbps = 0.0f;

    if (elapsed_ms > 0) {
        throughput_kbps = ((float)stats.total_bytes * 8.0f) / (float)elapsed_ms;  /* Kbps */
    }

    printf("\n========================================\n");
    printf("Transfer Statistics:\n");
    printf("========================================\n");
    printf("Blocks sent:        %d / %d\n", stats.blocks_sent, TOTAL_BLOCKS);
    printf("Total chunks:       %lu\n", stats.total_chunks);
    printf("Total bytes:        %lu (%.2f MB)\n",
           stats.total_bytes,
           (float)stats.total_bytes / (1024.0f * 1024.0f));
    printf("Elapsed time:       %lu.%03lu sec\n",
           elapsed_sec,
           elapsed_ms % 1000);
    printf("Throughput:         %.2f Kbps (%.2f KB/s)\n",
           throughput_kbps,
           throughput_kbps / 8.0f);
    printf("\nReliability:\n");
    printf("Disconnections:     %lu\n", stats.disconnections);
    printf("Retransmissions:    %d\n", stats.retransmits);
    printf("Congestion events:  %lu\n", stats.congestion_events);
    printf("Send failures:      %lu\n", stats.send_failures);
    if (stats.total_chunks > 0) {
        float success_rate = 100.0f * (1.0f - ((float)stats.send_failures / (float)(stats.total_chunks + stats.send_failures)));
        printf("Success rate:       %.2f%%\n", success_rate);
    }
    printf("========================================\n\n");
}

/*******************************************************************************
 *        Private Helper Functions
 *******************************************************************************/

/**
 * Generate waveform data for a block (with optional compression)
 * @param block_num Block number to generate
 * @param buffer Output buffer for compressed data
 * @return Actual size of generated block (compressed or raw)
 */
static uint32_t generate_block_data(uint16_t block_num, uint8_t *buffer)
{
#if BENCHMARK_MODE_ENABLED
    /* Benchmark mode: Use static waveforms for both compressed and uncompressed */
    static waveform_block_header_t waveform_header;

    /* Generate header with correct metadata (but CRC will be overwritten) */
    app_waveform_generate(block_num, &waveform_header, NULL);

    if (current_mode == TRANSFER_MODE_COMPRESSED) {
        /* COMPRESSED mode: use pre-compressed static data */

        /* Set correct CRC for compressed data */
        waveform_header.crc32 = STATIC_WAVEFORM_CRC32;

        /* Pack header + compressed data */
        memcpy(buffer, &waveform_header, sizeof(waveform_block_header_t));
        memcpy(buffer + sizeof(waveform_block_header_t),
               compressed_waveform_data,
               COMPRESSED_WAVEFORM_DATA_SIZE);

        uint32_t total_size = sizeof(waveform_block_header_t) + COMPRESSED_WAVEFORM_DATA_SIZE;

        /* Log for first few compressed blocks */
        if (block_num == current_block || (mode_switched && block_num < current_block + 3)) {
            printf("Block %d: STATIC COMPRESSED %d bytes (CRC:0x%08lX)\n",
                   block_num, (int)total_size, waveform_header.crc32);
        }

        return total_size;
    } else {
        /* UNCOMPRESSED mode: use static uncompressed data */

        /* Set correct CRC for uncompressed data */
        waveform_header.crc32 = STATIC_WAVEFORM_CRC32;

        /* Pack header + uncompressed data */
        memcpy(buffer, &waveform_header, sizeof(waveform_block_header_t));
        memcpy(buffer + sizeof(waveform_block_header_t),
               uncompressed_waveform_data,
               UNCOMPRESSED_WAVEFORM_DATA_SIZE);

        uint32_t total_size = sizeof(waveform_block_header_t) + UNCOMPRESSED_WAVEFORM_DATA_SIZE;

        /* Log for first few uncompressed blocks */
        if (block_num < 3) {
            printf("Block %d: STATIC UNCOMPRESSED %d bytes (CRC:0x%08lX)\n",
                   block_num, (int)total_size, waveform_header.crc32);
            printf("  First 12 data bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                   uncompressed_waveform_data[0], uncompressed_waveform_data[1],
                   uncompressed_waveform_data[2], uncompressed_waveform_data[3],
                   uncompressed_waveform_data[4], uncompressed_waveform_data[5],
                   uncompressed_waveform_data[6], uncompressed_waveform_data[7],
                   uncompressed_waveform_data[8], uncompressed_waveform_data[9],
                   uncompressed_waveform_data[10], uncompressed_waveform_data[11]);
            printf("  Verify: static array address = %p, size = %d\n",
                   (void*)uncompressed_waveform_data, UNCOMPRESSED_WAVEFORM_DATA_SIZE);

            /* Calculate CRC of the static data to verify it matches */
            uint32_t verify_crc = app_waveform_crc32(uncompressed_waveform_data, UNCOMPRESSED_WAVEFORM_DATA_SIZE);
            printf("  Static array CRC32: 0x%08lX (expected: 0x%08lX) %s\n",
                   verify_crc, STATIC_WAVEFORM_CRC32,
                   (verify_crc == STATIC_WAVEFORM_CRC32) ? "MATCH!" : "MISMATCH!");
        }

        return total_size;
    }
#elif USE_COMPRESSION
    /* Real compression mode (not benchmark) */
    static uint8_t raw_waveform_data[WAVEFORM_RAW_DATA_SIZE];
    static waveform_block_header_t waveform_header;

    if (!app_waveform_generate(block_num, &waveform_header, raw_waveform_data)) {
        printf("ERROR: Failed to generate waveform for block %d\n", block_num);
        return 0;
    }

    uint32_t compressed_size = 0;
    uint8_t *compressed_data_start = buffer + sizeof(waveform_block_header_t);

    if (!app_waveform_compress(&waveform_header, raw_waveform_data,
                               compressed_data_start, &compressed_size,
                               BLOCK_SIZE_MAX - sizeof(waveform_block_header_t))) {
        printf("ERROR: Failed to compress waveform for block %d\n", block_num);
        return 0;
    }

    memcpy(buffer, &waveform_header, sizeof(waveform_block_header_t));
    return sizeof(waveform_block_header_t) + compressed_size;
#else
    /* No compression - generate simulated waveform (uncompressed) */
    static waveform_block_header_t waveform_header;
    static uint8_t raw_waveform_data[WAVEFORM_RAW_DATA_SIZE];

    /* Generate simulated waveform */
    if (!app_waveform_generate(block_num, &waveform_header, raw_waveform_data)) {
        printf("ERROR: Failed to generate waveform for block %d\n", block_num);
        return 0;
    }

    /* Pack header + raw samples */
    memcpy(buffer, &waveform_header, sizeof(waveform_block_header_t));
    memcpy(buffer + sizeof(waveform_block_header_t), raw_waveform_data, WAVEFORM_RAW_DATA_SIZE);

    uint32_t total_size = sizeof(waveform_block_header_t) + WAVEFORM_RAW_DATA_SIZE;

    /* Log for first few blocks to verify */
    if (block_num < 3) {
        printf("Block %d: SIMULATED UNCOMPRESSED %d bytes (header=%d + samples=%d)\n",
               block_num, (int)total_size, (int)sizeof(waveform_block_header_t), WAVEFORM_RAW_DATA_SIZE);
    }

    return total_size;
#endif
}

/**
 * Send a chunk via GATT notification
 */
static bool send_chunk(uint16_t block_num, uint16_t chunk_num)
{
    /* Check if we have credits to send (flow control to prevent buffer overflow) */
    if (notification_credits <= 0) {
        /* No credits available - BLE stack buffer is full */
        uint32_t now = get_time_ms();
        if ((now - last_congestion_report_time) > CONGESTION_REPORT_INTERVAL_MS) {
            printf("[%lu ms] Flow control: waiting for transmission (credits=0, queued=%lu, transmitted=%lu)\n",
                   now, notifications_queued, notifications_transmitted);
            last_congestion_report_time = now;
        }
        return false;
    }

    /* Calculate total chunks needed for this block */
    uint16_t total_chunks_for_block = (current_block_size + actual_chunk_size - 1) / actual_chunk_size;

    /* CRITICAL: Use static buffer to prevent stack corruption
     * BLE stack may not copy data immediately - it might just store a pointer
     * and transmit later, by which time a local buffer would be invalid */
    static uint8_t packet[sizeof(chunk_header_t) + 512];  /* Max possible with our MTU setting */
    chunk_header_t *header = (chunk_header_t *)packet;

    /* Fill header */
    header->block_number = block_num;
    header->chunk_number = chunk_num;
    header->total_chunks = total_chunks_for_block;
    header->block_size_total = (uint16_t)current_block_size;

#if BENCHMARK_MODE_ENABLED
    /* Set compression flag based on current mode */
    header->flags = (current_mode == TRANSFER_MODE_COMPRESSED) ? 0x01 : 0x00;
#else
    header->flags = USE_COMPRESSION ? 0x01 : 0x00;  /* Bit 0: compressed */
#endif
    header->reserved = 0;

    /* Calculate chunk size (last chunk might be smaller) */
    uint32_t offset = chunk_num * actual_chunk_size;
    uint16_t this_chunk_size = actual_chunk_size;
    if (offset + actual_chunk_size > current_block_size) {
        this_chunk_size = current_block_size - offset;
    }
    header->chunk_size = this_chunk_size;

    /* Debug: log last chunk details */
    if (chunk_num == total_chunks_for_block - 1 && block_num < 20) {
        printf("[DEBUG] Last chunk B%d C%d: offset=%lu, size=%d, block_size=%lu\n",
               block_num, chunk_num, offset, this_chunk_size, current_block_size);
    }

    /* Copy data */
    memcpy(packet + sizeof(chunk_header_t), &block_data[offset], this_chunk_size);

    /* Send notification */
    uint16_t packet_size = sizeof(chunk_header_t) + this_chunk_size;

    /* Debug: log header bytes for last chunk of first 20 blocks */
    if (chunk_num == total_chunks_for_block - 1 && block_num < 20) {
        printf("[DEBUG] Header bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
               packet[0], packet[1], packet[2], packet[3], packet[4], packet[5],
               packet[6], packet[7], packet[8], packet[9], packet[10], packet[11]);
    }

    /* Log chunk sends for first 5 blocks to diagnose packet loss */
    uint32_t now = get_time_ms();
    if (block_num < 5) {
        printf("[%lu ms] Sending B%d C%d/%d (credits=%d)\n",
               now, block_num, chunk_num, actual_chunks_per_block - 1, notification_credits);
    }

    wiced_bt_gatt_status_t status = wiced_bt_gatt_server_send_notification(
        connection_id,
        HDLC_DATA_TRANSFER_SERVICE_DATA_BLOCK_VALUE,
        packet_size,
        packet,
        NULL
    );

    if (status != WICED_BT_GATT_SUCCESS) {
        /* Track failure */
        consecutive_send_failures++;
        consecutive_send_successes = 0;
        stats.send_failures++;

        /* Log failures for first 5 blocks */
        if (block_num < 5) {
            printf("[%lu ms] FAILED to send B%d C%d - status=0x%x (credits=%d)\n",
                   now, block_num, chunk_num, status, notification_credits);
        }

        if (status == WICED_BT_GATT_CONGESTED) {
            /* Stack is congested - adapt delay with granular additive backoff */
            if (consecutive_send_failures >= CONGESTION_THRESHOLD) {
                uint16_t old_delay = current_delay_ms;
                current_delay_ms = current_delay_ms + BACKOFF_INCREMENT;
                if (current_delay_ms > MAX_DELAY_MS) {
                    current_delay_ms = MAX_DELAY_MS;
                }

                /* Report congestion (throttled to avoid flooding console) */
                if ((now - last_congestion_report_time) > CONGESTION_REPORT_INTERVAL_MS) {
                    stats.congestion_events++;
                    printf("[%lu ms] WARNING: BLE congestion detected! ", now);
                    printf("Backing off: %d ms -> %d ms (failures: %d)\n",
                           old_delay, current_delay_ms, consecutive_send_failures);
                    last_congestion_report_time = now;
                }
            }
            return false;
        } else {
            printf("[%lu ms] ERROR: send_notification B%d C%d returned status 0x%x\n",
                   now, block_num, chunk_num, status);
            return false;
        }
    }

    /* Send successful - decrement credits and track stats */
    notification_credits--;
    notifications_queued++;
    consecutive_send_successes++;
    consecutive_send_failures = 0;

    /* Gradually speed up if we're having sustained success */
    if (consecutive_send_successes >= SUCCESS_THRESHOLD && current_delay_ms > MIN_DELAY_MS) {
        current_delay_ms -= SPEEDUP_DECREMENT;
        if (current_delay_ms < MIN_DELAY_MS) {
            current_delay_ms = MIN_DELAY_MS;
        }
        consecutive_send_successes = 0;  /* Reset counter */
    }

    return true;
}

/**
 * Get recommended delay based on current congestion
 */
uint16_t app_data_transfer_get_recommended_delay(void)
{
    return current_delay_ms;
}

/**
 * Reset transfer state
 */
static void reset_transfer_state(void)
{
    current_state = TRANSFER_STATE_IDLE;
    connection_id = 0;
    notifications_enabled = false;
    current_block = 0;
    current_chunk = 0;
    last_acked_block = 0;
    waiting_for_ack = false;
    memset(&stats, 0, sizeof(stats));

    /* Reset congestion tracking */
    consecutive_send_failures = 0;
    consecutive_send_successes = 0;
    current_delay_ms = INITIAL_DELAY_MS;
    last_congestion_report_time = 0;

    /* Reset flow control credits */
    notification_credits = MAX_NOTIFICATIONS_IN_FLIGHT;
    notifications_queued = 0;
    notifications_transmitted = 0;
}

/**
 * Get current time in milliseconds
 * Using FreeRTOS tick count
 */
static uint32_t get_time_ms(void)
{
    /* Use FreeRTOS ticks */
    extern uint32_t xTaskGetTickCount(void);
    return xTaskGetTickCount();  /* Assumes 1ms tick */
}

/**
 * Notification transmission complete callback
 * Called from GATT event handler when a notification has been transmitted
 */
void app_data_transfer_notification_sent(void)
{
    /* Increment credits - a notification slot is now available */
    if (notification_credits < MAX_NOTIFICATIONS_IN_FLIGHT) {
        notification_credits++;
        notifications_transmitted++;
    }
}

#if BENCHMARK_MODE_ENABLED
/**
 * Get current transfer mode
 */
transfer_mode_t app_data_transfer_get_mode(void)
{
    return current_mode;
}

/**
 * Get uncompressed mode statistics
 */
const mode_stats_t* app_data_transfer_get_uncompressed_stats(void)
{
    return &uncompressed_stats;
}

/**
 * Get compressed mode statistics
 */
const mode_stats_t* app_data_transfer_get_compressed_stats(void)
{
    return &compressed_stats;
}

/**
 * Print benchmark comparison statistics
 */
void app_data_transfer_print_benchmark_stats(void)
{
    printf("\n========================================\n");
    printf("BENCHMARK COMPARISON\n");
    printf("========================================\n");

    printf("\nUNCOMPRESSED Mode (7168 byte blocks):\n");
    printf("  Duration:    %lu.%03lu seconds\n",
           uncompressed_stats.duration_ms / 1000,
           uncompressed_stats.duration_ms % 1000);
    printf("  Blocks sent: %d\n", uncompressed_stats.blocks_sent);
    printf("  Data sent:   %lu bytes (%.2f MB)\n",
           uncompressed_stats.bytes_sent,
           (float)uncompressed_stats.bytes_sent / (1024.0f * 1024.0f));
    printf("  Throughput:  %.2f KB/s\n", uncompressed_stats.throughput_kbps);
    printf("  Block rate:  %.2f blocks/sec\n", uncompressed_stats.block_rate);

    printf("\nCOMPRESSED Mode (~3236 byte blocks):\n");
    printf("  Duration:    %lu.%03lu seconds\n",
           compressed_stats.duration_ms / 1000,
           compressed_stats.duration_ms % 1000);
    printf("  Blocks sent: %d\n", compressed_stats.blocks_sent);
    printf("  Data sent:   %lu bytes (%.2f MB)\n",
           compressed_stats.bytes_sent,
           (float)compressed_stats.bytes_sent / (1024.0f * 1024.0f));
    printf("  Throughput:  %.2f KB/s\n", compressed_stats.throughput_kbps);
    printf("  Block rate:  %.2f blocks/sec\n", compressed_stats.block_rate);

    printf("\nCOMPARISON:\n");
    if (uncompressed_stats.throughput_kbps > 0) {
        float speedup = compressed_stats.throughput_kbps / uncompressed_stats.throughput_kbps;
        printf("  Data throughput speedup:  %.2fx\n", speedup);
    }
    if (uncompressed_stats.block_rate > 0) {
        float block_speedup = compressed_stats.block_rate / uncompressed_stats.block_rate;
        printf("  Block rate speedup:       %.2fx\n", block_speedup);
    }

    /* Calculate effective waveform throughput (blocks per second) */
    printf("  \nEffective waveform data rate:\n");
    printf("    Uncompressed: %.2f waveforms/sec\n", uncompressed_stats.block_rate);
    printf("    Compressed:   %.2f waveforms/sec\n", compressed_stats.block_rate);

    printf("========================================\n\n");
}
#endif
