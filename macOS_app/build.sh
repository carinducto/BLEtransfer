#!/bin/bash
set -e

# Build script for macOS BLE Tester app
# This script compiles the Swift app and links it with the C++ driver library

echo "Building macOS BLE Tester..."

# Configuration
BUILD_DIR="build"
SHARED_DRIVER_DIR="../shared_driver"
DRIVER_LIB="$SHARED_DRIVER_DIR/build/libpsoc_driver.a"
DRIVER_INCLUDE="$SHARED_DRIVER_DIR/include"

# Check if driver library exists
if [ ! -f "$DRIVER_LIB" ]; then
    echo "Error: Driver library not found at $DRIVER_LIB"
    echo "Please build the shared_driver first:"
    echo "  cd $SHARED_DRIVER_DIR && mkdir -p build && cd build && cmake .. && make"
    exit 1
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Create bridging header
cat > "$BUILD_DIR/BridgingHeader.h" << 'EOF'
#ifndef BridgingHeader_h
#define BridgingHeader_h

#import "../../shared_driver/include/psoc_driver/psoc_driver.h"
#import "../../shared_driver/include/psoc_driver/protocol.h"
#import "../../shared_driver/include/psoc_driver/data_types.h"
#import "../../shared_driver/include/psoc_driver/crc32.h"
#import "../../shared_driver/include/psoc_driver/compression.h"
#import "../../shared_driver/include/psoc_driver/transfer_session.h"

#endif
EOF

# Compile Swift sources
echo "Compiling Swift sources..."
swiftc \
    -o "$BUILD_DIR/BLETester" \
    -import-objc-header "$BUILD_DIR/BridgingHeader.h" \
    -I "$DRIVER_INCLUDE" \
    -L "$SHARED_DRIVER_DIR/build" \
    -lpsoc_driver \
    -lz \
    -lc++ \
    -framework Foundation \
    -framework CoreBluetooth \
    -framework SwiftUI \
    -framework AppKit \
    Sources/PSoCDriverWrapper/PSoCDriver.swift \
    Sources/BLETester/BLEController.swift \
    Sources/BLETester/WaveformView.swift \
    Sources/BLETester/ContentView.swift \
    Sources/BLETester/BLEApp.swift

echo "Build successful! Executable: $BUILD_DIR/BLETester"
echo ""
echo "To run: ./$BUILD_DIR/BLETester"
