# macOS BLE Data Transfer Tester

A Swift command-line tool to test the PSoC6 Bluetooth LE data transfer service.

## Features

- ✅ Auto-scan and connect to "Inductosense Temp"
- ✅ Receives 1800 blocks × 7KB = 12.6 MB total
- ✅ Sends ACKs every 20 blocks
- ✅ Handles disconnection/reconnection automatically
- ✅ Real-time statistics and progress
- ✅ Validates data integrity (chunk numbers, block assembly)
- ✅ Measures throughput (Kbps, KB/s)

## Requirements

- macOS 13.0 or later
- Xcode 15+ (or Swift 5.9+)
- Bluetooth enabled

## How to Use

### Option 1: Run from Command Line

```bash
cd macOS_BLE_Tester
swift run
```

### Option 2: Open in Xcode

1. Open Xcode
2. File → Open → Select `macOS_BLE_Tester` folder
3. Click Run (⌘R) or Product → Run

### Option 3: Build and Run

```bash
cd macOS_BLE_Tester
swift build
./.build/debug/BLETester
```

## Usage Steps

1. **Flash PSoC6 firmware** (from parent directory):
   ```bash
   cd ..
   make program
   ```

2. **Power on PSoC6 board** - Wait for it to advertise

3. **Run the macOS tester**:
   ```bash
   cd macOS_BLE_Tester
   swift run
   ```

4. **Watch the transfer** - The app will:
   - Scan for "Inductosense Temp"
   - Connect automatically
   - Subscribe to notifications
   - Send START command
   - Receive and assemble all 1800 blocks
   - Display real-time progress
   - Print final statistics

## Expected Output

```
╔══════════════════════════════════════════════════════════════╗
║         PSoC6 BLE Data Transfer Tester for macOS            ║
║                                                              ║
║  Device: Inductosense Temp                                  ║
║  Total blocks: 1800                                          ║
║  Block size: 7168 bytes (7 KB)                          ║
║  Total data: ~12.6 MB                                        ║
╚══════════════════════════════════════════════════════════════╝

🔍 Scanning for 'Inductosense Temp'...
✅ Bluetooth powered on
🎯 Found device: Inductosense Temp
   UUID: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
   RSSI: -45 dBm
🔗 Connecting...
✅ Connected!
🔍 Discovering services...
✅ Found Data Transfer Service
🔍 Discovering characteristics...
✅ Found Data Block characteristic
✅ Found Control characteristic
✅ Subscribed to Data Block notifications

============================================================
🚀 Ready to start transfer!
============================================================
✅ START command sent
📊 Waiting for data...
📦 Block 10/1800 (0.6%) | 15.2 KB/s | 295 chunks
📦 Block 20/1800 (1.1%) | 16.8 KB/s | 590 chunks
📤 ACK sent for blocks 0-19
...
📦 Block 1800/1800 (100.0%) | 18.5 KB/s | 53100 chunks

============================================================
🎉 TRANSFER COMPLETE!
============================================================
📊 Statistics:
   Blocks received:  1800 / 1800
   Total chunks:     53100
   Total bytes:      12902400 (12.30 MB)
   Elapsed time:     682.45 seconds
   Throughput:       151.26 Kbps (18.91 KB/s)
============================================================
```

## Troubleshooting

**"Bluetooth unauthorized"**
- Go to System Settings → Privacy & Security → Bluetooth
- Enable Bluetooth for Terminal (or Xcode)

**Can't find device**
- Check PSoC6 is powered on and advertising
- Check serial terminal shows "Bluetooth Management Event: BTM_ENABLED_EVT"
- Try resetting the PSoC6 board

**Transfer interrupted**
- The app will automatically try to reconnect
- PSoC6 firmware will resume from last ACK'd block

**Build errors**
- Make sure you're using Swift 5.9+ (`swift --version`)
- Make sure Xcode Command Line Tools are installed

## Testing Different Scenarios

### Test Reconnection
1. Start transfer
2. Power off PSoC6 mid-transfer
3. Power back on
4. Watch it resume from last ACK

### Test at Different Distances
- Move Mac closer/farther from PSoC6
- Observe throughput changes
- Check for packet loss

## UUIDs Used

```
Service:      A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D
Data Block:   A1B2C3D5-E5F6-4A5B-8C9D-0E1F2A3B4C5D
Control:      A1B2C3D6-E5F6-4A5B-8C9D-0E1F2A3B4C5D
```

## Protocol Details

### Chunk Header (8 bytes)
- Block Number (2 bytes) - Current block 0-1799
- Chunk Number (2 bytes) - Chunk within block
- Chunk Size (2 bytes) - Data bytes in this chunk
- Total Chunks (2 bytes) - Total chunks in block (~30)

### Control Messages (7 bytes)
- Command (1 byte) - 0x01=START, 0x02=STOP, 0x03=ACK
- Block Number (2 bytes) - For ACK
- Timestamp (4 bytes) - Unix timestamp

## License

Part of the PSoC6 BLE Environmental Sensing Service example.
