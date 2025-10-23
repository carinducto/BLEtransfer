/*******************************************************************************
 * File Name: app_data_transfer.h
 *
 * Description: This file contains the function prototypes and constants for
 *              the data transfer service.
 *
 *******************************************************************************/

#ifndef APP_DATA_TRANSFER_H_
#define APP_DATA_TRANSFER_H_

#include <stdint.h>
#include <stdbool.h>
#include "wiced_bt_gatt.h"

/*******************************************************************************
 *        Constants
 *******************************************************************************/

/* Data transfer configuration */
#define BLOCK_SIZE_RAW              (7168u)     /* 7KB per block (uncompressed) */
#define BLOCK_SIZE_MAX              (7168u)     /* Maximum block size (for buffer allocation) */
#define TOTAL_BLOCKS                (1800u)     /* Total number of blocks */
#define CHUNK_SIZE                  (244u)      /* Max chunk size per notification (MTU-3 for ATT overhead) */
#define CHUNKS_PER_BLOCK            ((BLOCK_SIZE_MAX + CHUNK_SIZE - 1) / CHUNK_SIZE)  /* ~30 chunks per block */
#define ACK_INTERVAL                (20u)       /* Send ACK every 20 blocks */

/* Benchmark test configuration */
#define BENCHMARK_MODE_ENABLED      (0)         /* Disabled - uncompressed mode only for demo */
#define BENCHMARK_UNCOMPRESSED_DURATION_MS  (120000u)  /* 2 minutes in uncompressed mode */

/* Compression configuration */
#define USE_COMPRESSION             (0)         /* Enable waveform compression (disabled for now) */
#define USE_PRECOMPRESSED_DATA      (1)         /* Use pre-compressed static waveform for benchmark */

/* Transfer modes */
typedef enum {
    TRANSFER_MODE_UNCOMPRESSED,     /* Send uncompressed 7KB blocks */
    TRANSFER_MODE_COMPRESSED        /* Send compressed ~3KB blocks */
} transfer_mode_t;

/* Control message types */
#define CTRL_CMD_START              (0x01)      /* Start transfer command */
#define CTRL_CMD_STOP               (0x02)      /* Stop transfer command */
#define CTRL_CMD_ACK                (0x03)      /* Acknowledgment */
#define CTRL_CMD_REQUEST_RESUME     (0x04)      /* Request resume info */
#define CTRL_CMD_RESUME_RESPONSE    (0x05)      /* Resume response */

/* Transfer states */
typedef enum {
    TRANSFER_STATE_IDLE,
    TRANSFER_STATE_ACTIVE,
    TRANSFER_STATE_PAUSED,
    TRANSFER_STATE_WAITING_ACK,
    TRANSFER_STATE_COMPLETE
} transfer_state_t;

/* Data chunk header structure (prepended to each notification) */
typedef struct __attribute__((packed)) {
    uint16_t block_number;      /* Current block number (0-1799) */
    uint16_t chunk_number;      /* Chunk within block (0-29) */
    uint16_t chunk_size;        /* Size of data in this chunk */
    uint16_t total_chunks;      /* Total chunks in this block */
    uint16_t block_size_total;  /* Total size of this block (compressed) */
    uint8_t  flags;             /* Bit 0: compressed (1) or raw (0) */
    uint8_t  reserved;          /* Reserved for future use */
} chunk_header_t;

/* Control message structure */
typedef struct __attribute__((packed)) {
    uint8_t command;            /* Command type */
    uint16_t block_number;      /* Block number for ACK or resume */
    uint32_t timestamp;         /* Timestamp for debugging */
} control_msg_t;

/* Transfer statistics */
typedef struct {
    uint32_t start_time_ms;     /* Transfer start time */
    uint32_t end_time_ms;       /* Transfer end time */
    uint32_t total_bytes;       /* Total bytes transferred */
    uint32_t total_chunks;      /* Total chunks sent */
    uint16_t blocks_sent;       /* Blocks successfully sent */
    uint16_t retransmits;       /* Number of retransmissions */
    uint32_t disconnections;    /* Number of disconnections during transfer */
    uint32_t congestion_events; /* Number of times congestion detected */
    uint32_t send_failures;     /* Total send failures */
} transfer_stats_t;

/* Per-mode statistics for benchmark comparison */
typedef struct {
    transfer_mode_t mode;       /* Mode for these stats */
    uint32_t start_time_ms;     /* Mode start time */
    uint32_t duration_ms;       /* Time in this mode */
    uint32_t bytes_sent;        /* Bytes sent in this mode */
    uint16_t blocks_sent;       /* Blocks sent in this mode */
    float throughput_kbps;      /* Calculated throughput in KB/s */
    float block_rate;           /* Blocks per second */
} mode_stats_t;

/*******************************************************************************
 *        Function Prototypes
 *******************************************************************************/

/**
 * Initialize the data transfer module
 */
void app_data_transfer_init(void);

/**
 * Start data transfer
 * @param conn_id Connection ID
 * @return true if started successfully
 */
bool app_data_transfer_start(uint16_t conn_id);

/**
 * Stop data transfer
 */
void app_data_transfer_stop(void);

/**
 * Pause data transfer (on disconnection)
 */
void app_data_transfer_pause(void);

/**
 * Resume data transfer (on reconnection)
 * @param conn_id Connection ID
 * @return true if resumed successfully
 */
bool app_data_transfer_resume(uint16_t conn_id);

/**
 * Process next chunk - call this from a task/timer
 * @return true if more chunks to send, false if complete
 */
bool app_data_transfer_process_next_chunk(void);

/**
 * Handle control characteristic write from phone
 * @param conn_id Connection ID
 * @param p_write_req Write request data
 */
void app_data_transfer_control_write_handler(uint16_t conn_id, wiced_bt_gatt_write_req_t *p_write_req);

/**
 * Handle CCCD write for data block characteristic
 * @param conn_id Connection ID
 * @param notifications_enabled true if notifications enabled
 */
void app_data_transfer_cccd_write_handler(uint16_t conn_id, bool notifications_enabled);

/**
 * Get current transfer statistics
 * @return Pointer to statistics structure
 */
const transfer_stats_t* app_data_transfer_get_stats(void);

/**
 * Get current transfer state
 * @return Current state
 */
transfer_state_t app_data_transfer_get_state(void);

/**
 * Print transfer statistics to console
 */
void app_data_transfer_print_stats(void);

/**
 * Set MTU for data transfer (called after MTU exchange)
 * @param mtu Negotiated MTU value
 */
void app_data_transfer_set_mtu(uint16_t mtu);

/**
 * Get recommended delay in milliseconds based on current congestion
 * @return Recommended delay in milliseconds
 */
uint16_t app_data_transfer_get_recommended_delay(void);

/**
 * Notification transmission complete callback
 * Called when a notification has been successfully transmitted
 */
void app_data_transfer_notification_sent(void);

/**
 * Get current transfer mode
 * @return Current transfer mode (UNCOMPRESSED or COMPRESSED)
 */
transfer_mode_t app_data_transfer_get_mode(void);

/**
 * Get statistics for uncompressed mode
 * @return Pointer to uncompressed mode statistics
 */
const mode_stats_t* app_data_transfer_get_uncompressed_stats(void);

/**
 * Get statistics for compressed mode
 * @return Pointer to compressed mode statistics
 */
const mode_stats_t* app_data_transfer_get_compressed_stats(void);

/**
 * Print benchmark comparison statistics
 */
void app_data_transfer_print_benchmark_stats(void);

#endif /* APP_DATA_TRANSFER_H_ */
