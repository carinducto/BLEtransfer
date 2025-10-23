# PSoC Driver Library

Cross-platform C++ library for communicating with PSoC BLE devices using the Inductosense transfer protocol.

## Overview

This library provides the core protocol logic, data processing, and state management for transferring ultrasound waveform data from PSoC6 devices over Bluetooth LE. It is designed to be platform-agnostic and can be used from both macOS (Swift) and Windows (C#/.NET) applications.

## Architecture

The library implements **Layer 3** (Driver/Protocol) of the three-layer architecture:

- **Layer 1**: UI (platform-specific - SwiftUI on macOS, WPF on Windows)
- **Layer 2**: BLE Interface (platform-specific - CoreBluetooth on macOS, Windows.Devices.Bluetooth on Windows)
- **Layer 3**: Driver Library (this library - shared across platforms)

## Features

- Protocol constants and data structures
- CRC32 validation for data integrity
- Zlib decompression with delta decoding
- Block/chunk reassembly state machine
- Transfer session management
- Statistics tracking
- Callback-based event notification

## Building

### Prerequisites

- CMake 3.15 or higher
- C++11 compatible compiler
- zlib library

### macOS

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

The library will be built as `libpsoc_driver.a`.

### Windows

```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

The library will be built as `psoc_driver.lib`.

## Usage

### C/C++

```c
#include "psoc_driver/psoc_driver.h"

// Initialize library
psoc_driver_init();

// Create transfer session
transfer_session_t* session = transfer_session_create();

// Set callbacks
transfer_session_set_waveform_callback(session, on_waveform, user_data);
transfer_session_set_progress_callback(session, on_progress, user_data);
transfer_session_set_completion_callback(session, on_completion, user_data);
transfer_session_set_ack_callback(session, on_ack, user_data);

// Start transfer
transfer_session_start(session);

// Process chunks as they arrive from BLE
transfer_session_process_chunk(session, chunk_data, chunk_length);

// Cleanup
transfer_session_destroy(session);
psoc_driver_cleanup();
```

### Swift (macOS)

See `../macOS_app/` for Swift integration example using C interop.

### C# (Windows)

See `../Windows_app/` for C# integration example using P/Invoke.

## API Reference

See header files in `include/psoc_driver/` for full API documentation.

## License

See LICENSE file in the repository root.
