#include "psoc_driver/transfer_session.h"
#include "psoc_driver/compression.h"
#include "psoc_driver/crc32.h"
#include <cstring>
#include <map>
#include <vector>
#include <chrono>
#include <cstdio>

struct transfer_session {
    // State
    bool is_active;
    std::chrono::steady_clock::time_point start_time;

    // Block tracking
    std::map<uint16_t, std::vector<uint8_t>> received_blocks;
    std::map<uint16_t, std::map<uint16_t, std::vector<uint8_t>>> block_chunks;
    std::map<uint16_t, uint16_t> block_expected_chunks;
    uint16_t last_acked_block;

    // Statistics
    uint32_t total_bytes_received;
    uint32_t total_chunks_received;

    // Callbacks
    waveform_callback_t waveform_callback;
    void* waveform_user_data;

    progress_callback_t progress_callback;
    void* progress_user_data;

    completion_callback_t completion_callback;
    void* completion_user_data;

    ack_callback_t ack_callback;
    void* ack_user_data;
};

transfer_session_t* transfer_session_create(void) {
    transfer_session_t* session = new transfer_session_t();
    session->is_active = false;
    session->last_acked_block = 0;
    session->total_bytes_received = 0;
    session->total_chunks_received = 0;
    session->waveform_callback = nullptr;
    session->waveform_user_data = nullptr;
    session->progress_callback = nullptr;
    session->progress_user_data = nullptr;
    session->completion_callback = nullptr;
    session->completion_user_data = nullptr;
    session->ack_callback = nullptr;
    session->ack_user_data = nullptr;
    return session;
}

void transfer_session_destroy(transfer_session_t* session) {
    if (session) {
        delete session;
    }
}

void transfer_session_set_waveform_callback(transfer_session_t* session, waveform_callback_t callback, void* user_data) {
    session->waveform_callback = callback;
    session->waveform_user_data = user_data;
}

void transfer_session_set_progress_callback(transfer_session_t* session, progress_callback_t callback, void* user_data) {
    session->progress_callback = callback;
    session->progress_user_data = user_data;
}

void transfer_session_set_completion_callback(transfer_session_t* session, completion_callback_t callback, void* user_data) {
    session->completion_callback = callback;
    session->completion_user_data = user_data;
}

void transfer_session_set_ack_callback(transfer_session_t* session, ack_callback_t callback, void* user_data) {
    session->ack_callback = callback;
    session->ack_user_data = user_data;
}

void transfer_session_start(transfer_session_t* session) {
    session->is_active = true;
    session->start_time = std::chrono::steady_clock::now();
    session->received_blocks.clear();
    session->block_chunks.clear();
    session->block_expected_chunks.clear();
    session->last_acked_block = 0;
    session->total_bytes_received = 0;
    session->total_chunks_received = 0;
}

void transfer_session_stop(transfer_session_t* session) {
    session->is_active = false;
}

static void parse_waveform_header(const uint8_t* data, waveform_header_t* header) {
    header->block_number = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    header->timestamp_ms = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    header->sample_rate_hz = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
    header->sample_count = data[12] | (data[13] << 8);
    header->trigger_sample = data[16] | (data[17] << 8);
    header->pulse_freq_hz = data[18] | (data[19] << 8) | (data[20] << 16) | (data[21] << 24);
    header->temperature_cx10 = (int16_t)(data[26] | (data[27] << 8));
    header->gain_db = data[28];
    header->crc32 = data[30] | (data[31] << 8) | (data[32] << 16) | (data[33] << 24);
}

static void unpack_24bit_samples(const uint8_t* sample_data, size_t sample_count, int32_t* samples) {
    for (size_t i = 0; i < sample_count; i++) {
        size_t offset = i * 3;
        int32_t sample = sample_data[offset] |
                        (sample_data[offset + 1] << 8) |
                        (sample_data[offset + 2] << 16);

        // Sign extend from 24-bit to 32-bit
        if (sample & 0x800000) {
            sample |= 0xFF000000;
        }

        samples[i] = sample;
    }
}

static bool process_uncompressed_block(const uint8_t* block_data, size_t block_size, waveform_data_t* waveform) {
    if (block_size < WAVEFORM_HEADER_SIZE + 7128) {
        return false;
    }

    // Parse header
    parse_waveform_header(block_data, &waveform->header);

    // Unpack 24-bit samples
    const uint8_t* sample_data = block_data + WAVEFORM_HEADER_SIZE;
    unpack_24bit_samples(sample_data, SAMPLES_PER_WAVEFORM, waveform->samples);

    return true;
}

static bool process_compressed_block(const uint8_t* block_data, size_t block_size, waveform_data_t* waveform) {
    if (block_size < WAVEFORM_HEADER_SIZE) {
        return false;
    }

    // Parse header
    parse_waveform_header(block_data, &waveform->header);

    // Decompress data
    const uint8_t* compressed_data = block_data + WAVEFORM_HEADER_SIZE;
    size_t compressed_size = block_size - WAVEFORM_HEADER_SIZE;

    if (!decompress_waveform(compressed_data, compressed_size, waveform->samples)) {
        return false;
    }

    // Verify CRC
    uint32_t calculated_crc = calculate_crc32_samples(waveform->samples, SAMPLES_PER_WAVEFORM);
    if (calculated_crc != waveform->header.crc32) {
        return false;
    }

    return true;
}

bool transfer_session_process_chunk(transfer_session_t* session, const uint8_t* data, size_t length) {
    if (length < 12) {
        return false;
    }

    // Parse chunk header
    uint16_t block_number = data[0] | (data[1] << 8);
    uint16_t chunk_number = data[2] | (data[3] << 8);
    uint16_t chunk_size = data[4] | (data[5] << 8);
    uint16_t total_chunks = data[6] | (data[7] << 8);

    // Validate block number
    if (block_number >= TOTAL_BLOCKS) {
        return false;
    }

    // Extract chunk data
    std::vector<uint8_t> chunk_data(data + 12, data + 12 + chunk_size);

    // Store chunk
    if (session->block_chunks.find(block_number) == session->block_chunks.end()) {
        session->block_chunks[block_number] = std::map<uint16_t, std::vector<uint8_t>>();
        session->block_expected_chunks[block_number] = total_chunks;
    }
    session->block_chunks[block_number][chunk_number] = chunk_data;

    session->total_chunks_received++;
    session->total_bytes_received += chunk_size;

    // Check if block is complete
    if (session->block_chunks[block_number].size() == total_chunks) {
        // Reassemble block
        std::vector<uint8_t> block_data;
        for (uint16_t i = 0; i < total_chunks; i++) {
            const auto& chunk = session->block_chunks[block_number][i];
            block_data.insert(block_data.end(), chunk.begin(), chunk.end());
        }

        // Process waveform
        waveform_data_t waveform;
        bool is_compressed = (block_data.size() < BLOCK_SIZE); // Heuristic: compressed blocks are smaller
        bool success;

        if (is_compressed) {
            success = process_compressed_block(block_data.data(), block_data.size(), &waveform);
        } else {
            success = process_uncompressed_block(block_data.data(), block_data.size(), &waveform);
        }

        if (success && session->waveform_callback) {
            session->waveform_callback(&waveform, is_compressed, session->waveform_user_data);
        }

        // Mark block as received
        session->received_blocks[block_number] = block_data;
        session->block_chunks.erase(block_number);
        session->block_expected_chunks.erase(block_number);

        // Send ACK if needed
        bool should_ack = block_number > 0 && (block_number + 1) % ACK_INTERVAL == 0;
        if (should_ack && session->ack_callback) {
            session->ack_callback(block_number, session->ack_user_data);
        }

        // Update progress
        if (session->progress_callback) {
            transfer_stats_t stats;
            transfer_session_get_stats(session, &stats);
            session->progress_callback(&stats, session->progress_user_data);
        }

        // Check if transfer is complete
        if (session->received_blocks.size() == TOTAL_BLOCKS) {
            session->is_active = false;
            if (session->completion_callback) {
                transfer_stats_t stats;
                transfer_session_get_stats(session, &stats);
                session->completion_callback(&stats, session->completion_user_data);
            }
        }
    }

    return true;
}

void transfer_session_get_stats(const transfer_session_t* session, transfer_stats_t* stats) {
    stats->blocks_received = session->received_blocks.size();
    stats->total_blocks = TOTAL_BLOCKS;
    stats->total_bytes_received = session->total_bytes_received;
    stats->total_chunks_received = session->total_chunks_received;

    if (session->is_active) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - session->start_time);
        stats->elapsed_seconds = elapsed.count() / 1000.0;

        if (stats->elapsed_seconds > 0) {
            stats->throughput_kbps = (stats->total_bytes_received / stats->elapsed_seconds) / 1000.0;
        } else {
            stats->throughput_kbps = 0.0;
        }
    } else {
        stats->elapsed_seconds = 0.0;
        stats->throughput_kbps = 0.0;
    }

    stats->progress_percent = (stats->blocks_received * 100.0) / stats->total_blocks;
}

bool transfer_session_is_active(const transfer_session_t* session) {
    return session->is_active;
}
