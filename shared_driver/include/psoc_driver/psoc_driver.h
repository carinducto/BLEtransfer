#ifndef PSOC_DRIVER_H
#define PSOC_DRIVER_H

/**
 * PSoC BLE Driver - Main Interface
 *
 * This is the primary header to include when using the PSoC driver library.
 * It provides a complete C API for communication with PSoC BLE devices
 * following the Inductosense transfer protocol.
 */

#include "protocol.h"
#include "data_types.h"
#include "crc32.h"
#include "compression.h"
#include "transfer_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the library version string
 * @return Version string (e.g., "1.0.0")
 */
const char* psoc_driver_version(void);

/**
 * Initialize the driver library
 * Must be called before using any other driver functions
 * @return true on success, false on failure
 */
bool psoc_driver_init(void);

/**
 * Cleanup the driver library
 * Should be called when done using the driver
 */
void psoc_driver_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // PSOC_DRIVER_H
