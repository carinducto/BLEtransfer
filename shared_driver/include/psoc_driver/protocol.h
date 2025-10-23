#ifndef PSOC_PROTOCOL_H
#define PSOC_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// BLE Service and Characteristic UUIDs
#define SERVICE_UUID "A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D"
#define DATA_BLOCK_UUID "A1B2C3D5-E5F6-4A5B-8C9D-0E1F2A3B4C5D"
#define CONTROL_UUID "A1B2C3D6-E5F6-4A5B-8C9D-0E1F2A3B4C5D"

// Device identification
#define DEVICE_NAME "Inductosense Temp"

// Transfer constants
#define TOTAL_BLOCKS 1800
#define BLOCK_SIZE 7168
#define ACK_INTERVAL 20

// Control commands
#define CMD_START 0x01
#define CMD_STOP  0x02
#define CMD_ACK   0x03

// Waveform constants
#define SAMPLES_PER_WAVEFORM 2376
#define BYTES_PER_SAMPLE 3
#define WAVEFORM_HEADER_SIZE 38

#ifdef __cplusplus
}
#endif

#endif // PSOC_PROTOCOL_H
