# Three-Layer Architecture Documentation

## Overview

This project has been refactored into a clean three-layer architecture that separates platform-specific code from the core protocol logic, enabling code reuse across macOS and Windows platforms.

## Architecture Layers

### Layer 3: Shared Driver Library (C++)
**Location:** `shared_driver/`
**Purpose:** Platform-agnostic protocol implementation
**Language:** C++11 with C API for interop

**Components:**
- `protocol.h` - Protocol constants (UUIDs, commands, transfer parameters)
- `data_types.h` - Data structures (waveform header, statistics, etc.)
- `crc32.cpp/h` - CRC32 validation for data integrity
- `compression.cpp/h` - Zlib decompression with delta decoding
- `transfer_session.cpp/h` - State machine for block/chunk reassembly
- `psoc_driver.cpp/h` - Main library interface and initialization

**Key Features:**
- Callback-based event notification (waveforms, progress, completion, ACKs)
- Thread-safe state management
- Memory efficient chunk reassembly
- Automatic CRC validation
- Statistics tracking

**Building:**
```bash
cd shared_driver
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make  # macOS/Linux
# OR
cmake --build . --config Release  # Windows
```

Output: `libpsoc_driver.a` (macOS) or `psoc_driver.lib` (Windows)

### Layer 2: BLE Interface Control (Platform-Specific)

#### macOS Implementation
**Location:** `macOS_app/Sources/BLETester/BLEController.swift`
**Framework:** CoreBluetooth

**Responsibilities:**
- Device scanning and connection management
- GATT service/characteristic discovery
- BLE notification subscriptions
- Command transmission (start/stop/ack)
- Delegates all protocol logic to Layer 3

#### Windows Implementation
**Location:** `Windows_app/Controllers/BLEController.cs`
**Framework:** Windows.Devices.Bluetooth

**Responsibilities:**
- Device enumeration and connection
- GATT operations
- Notification handling
- Command transmission
- Delegates all protocol logic to Layer 3

### Layer 1: UI (Platform-Specific)

#### macOS UI
**Location:** `macOS_app/Sources/BLETester/`
**Framework:** SwiftUI

**Components:**
- `BLEApp.swift` - Application entry point
- `ContentView.swift` - Main window with status, controls, and layout
- `WaveformView.swift` - Waveform visualization with Canvas API

#### Windows UI
**Location:** `Windows_app/`
**Framework:** WPF (.NET 8)

**Components:**
- `App.xaml` - Application entry point
- `MainWindow.xaml` - Main window XAML layout
- `MainWindow.xaml.cs` - Code-behind with event handlers
- `Views/WaveformView.xaml` - Waveform visualization control

## Data Flow

```
PSoC Device (BLE)
       ↓
Layer 2: BLE Interface
    • Receives BLE notifications
    • Extracts raw chunk data
       ↓
Layer 3: Shared Driver
    • Reassembles chunks into blocks
    • Decompresses waveform data
    • Validates CRC
    • Invokes callbacks
       ↓
Layer 2: BLE Interface
    • Wraps C data into platform types
    • Dispatches to UI thread
       ↓
Layer 1: UI
    • Updates visualizations
    • Displays statistics
```

## Cross-Platform Interop

### macOS (Swift → C++)
**Mechanism:** Bridging Header
**File:** `macOS_app/build/BridgingHeader.h`

**Wrapper:** `macOS_app/Sources/PSoCDriverWrapper/PSoCDriver.swift`
- Wraps C types in Swift structs
- Manages callback lifetimes with Unmanaged pointers
- Converts C arrays to Swift Arrays

### Windows (C# → C++)
**Mechanism:** P/Invoke
**File:** `Windows_app/Interop/NativeMethods.cs`

**Wrapper:** `Windows_app/Interop/PSoCDriver.cs`
- Marshals C structures to C# classes
- Manages unmanaged memory lifetimes
- Converts callbacks to C# events

## Building

### macOS Application

```bash
cd macOS_app
./build.sh
./build/BLETester
```

Requirements:
- macOS 13.0+
- Xcode Command Line Tools
- Shared driver library built

### Windows Application

```powershell
cd Windows_app
dotnet build -c Release
dotnet run
```

Requirements:
- Windows 10/11
- .NET 8.0 SDK
- Visual Studio 2022 (optional)
- Shared driver library built for Windows

## Benefits of This Architecture

1. **Code Reuse:** Protocol logic written once, used on both platforms
2. **Maintainability:** Bug fixes in Layer 3 benefit both apps
3. **Testability:** Core logic can be unit tested independently
4. **Scalability:** Easy to add Linux, iOS, or Android support
5. **Performance:** Native C++ for performance-critical code
6. **Clean Separation:** Each layer has a single responsibility

## Future Extensions

### Adding New Platforms

To add support for a new platform (e.g., Linux, iOS, Android):

1. **Reuse Layer 3** (no changes needed)
2. **Implement Layer 2** using platform's BLE API
   - Linux: BlueZ
   - iOS: CoreBluetooth (similar to macOS)
   - Android: android.bluetooth
3. **Create Layer 1** with platform's UI framework
   - Linux: GTK, Qt
   - iOS: SwiftUI
   - Android: Jetpack Compose

### Adding New Features

- **Compression improvements:** Modify `compression.cpp` only
- **New protocol commands:** Update `protocol.h` and `transfer_session.cpp`
- **UI enhancements:** Modify platform-specific UI files only
- **Statistics:** Add fields to `data_types.h` and update stats calculations

## File Organization Summary

```
Bluetooth_LE_Environmental_Sensing_Service/
├── shared_driver/           # Layer 3: C++ Driver Library
│   ├── include/psoc_driver/
│   ├── src/
│   ├── CMakeLists.txt
│   └── README.md
│
├── macOS_app/              # macOS Application
│   ├── Sources/
│   │   ├── PSoCDriverWrapper/  # Swift wrapper
│   │   └── BLETester/          # Layer 1 & 2
│   ├── build.sh
│   └── Package.swift
│
└── Windows_app/            # Windows Application
    ├── Interop/            # C# wrapper
    ├── Controllers/        # Layer 2
    ├── Views/              # Layer 1 (partial)
    ├── MainWindow.xaml     # Layer 1
    ├── BLETester.csproj
    └── README.md
```

## Performance Characteristics

- **Throughput:** 20-50 KB/s typical (BLE 4.2/5.0 dependent)
- **Memory:** ~10 MB per app + driver library
- **CPU:** Minimal (<5%) during active transfer
- **Latency:** <100ms chunk processing time

## Troubleshooting

### macOS Build Issues
- Ensure shared driver is built first
- Check that libpsoc_driver.a exists in `shared_driver/build/`
- Verify zlib is available (`brew install zlib` if needed)

### Windows Build Issues
- Build shared driver with matching architecture (x64)
- Ensure Windows SDK is installed
- Check that psoc_driver.lib is in correct path

### Runtime Issues
- **Connection failures:** Check Bluetooth permissions
- **Transfer stalls:** Verify MTU negotiation (check logs)
- **CRC errors:** May indicate BLE packet loss (check signal strength)

## License

See LICENSE file in repository root.
