#ifndef PSOC_TRANSFER_SESSION_H
#define PSOC_TRANSFER_SESSION_H

#include "data_types.h"
#include "protocol.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle for transfer session
typedef struct transfer_session transfer_session_t;

// Callback types
typedef void (*waveform_callback_t)(const waveform_data_t* waveform, bool is_compressed, void* user_data);
typedef void (*progress_callback_t)(const transfer_stats_t* stats, void* user_data);
typedef void (*completion_callback_t)(const transfer_stats_t* final_stats, void* user_data);
typedef void (*ack_callback_t)(uint16_t block_number, void* user_data);

/**
 * Create a new transfer session
 * @return Pointer to new session, or NULL on failure
 */
transfer_session_t* transfer_session_create(void);

/**
 * Destroy a transfer session and free resources
 * @param session Session to destroy
 */
void transfer_session_destroy(transfer_session_t* session);

/**
 * Set waveform callback (called when a complete waveform is received)
 * @param session Transfer session
 * @param callback Callback function
 * @param user_data User data to pass to callback
 */
void transfer_session_set_waveform_callback(transfer_session_t* session, waveform_callback_t callback, void* user_data);

/**
 * Set progress callback (called periodically during transfer)
 * @param session Transfer session
 * @param callback Callback function
 * @param user_data User data to pass to callback
 */
void transfer_session_set_progress_callback(transfer_session_t* session, progress_callback_t callback, void* user_data);

/**
 * Set completion callback (called when transfer completes)
 * @param session Transfer session
 * @param callback Callback function
 * @param user_data User data to pass to callback
 */
void transfer_session_set_completion_callback(transfer_session_t* session, completion_callback_t callback, void* user_data);

/**
 * Set ACK callback (called when an ACK needs to be sent)
 * @param session Transfer session
 * @param callback Callback function
 * @param user_data User data to pass to callback
 */
void transfer_session_set_ack_callback(transfer_session_t* session, ack_callback_t callback, void* user_data);

/**
 * Start a new transfer session
 * @param session Transfer session
 */
void transfer_session_start(transfer_session_t* session);

/**
 * Stop an active transfer session
 * @param session Transfer session
 */
void transfer_session_stop(transfer_session_t* session);

/**
 * Process a received data chunk
 * @param session Transfer session
 * @param data Pointer to chunk data
 * @param length Length of chunk data
 * @return true if chunk was processed successfully, false otherwise
 */
bool transfer_session_process_chunk(transfer_session_t* session, const uint8_t* data, size_t length);

/**
 * Get current transfer statistics
 * @param session Transfer session
 * @param stats Pointer to stats structure to fill
 */
void transfer_session_get_stats(const transfer_session_t* session, transfer_stats_t* stats);

/**
 * Check if transfer is active
 * @param session Transfer session
 * @return true if transfer is active, false otherwise
 */
bool transfer_session_is_active(const transfer_session_t* session);

#ifdef __cplusplus
}
#endif

#endif // PSOC_TRANSFER_SESSION_H
