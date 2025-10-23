import Foundation
import CoreBluetooth
import Compression
import Combine
import SwiftUI

// UUIDs from PSoC6 firmware
let SERVICE_UUID = CBUUID(string: "A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D")
let DATA_BLOCK_UUID = CBUUID(string: "A1B2C3D5-E5F6-4A5B-8C9D-0E1F2A3B4C5D")
let CONTROL_UUID = CBUUID(string: "A1B2C3D6-E5F6-4A5B-8C9D-0E1F2A3B4C5D")

// Transfer constants
let DEVICE_NAME = "Inductosense Temp"
let TOTAL_BLOCKS = 1800
let BLOCK_SIZE = 7168
let ACK_INTERVAL = 20

// Control commands
let CMD_START: UInt8 = 0x01
let CMD_STOP: UInt8 = 0x02
let CMD_ACK: UInt8 = 0x03

// Helper functions for waveform decompression
func decompressWaveform(compressedData: Data) -> [Int32]? {
    // Expected size: 2376 samples * 2 bytes (int16) = 4752 bytes (delta-encoded)
    let expectedDecompressedSize = 2376 * 2

    var decompressedData = Data(count: expectedDecompressedSize)
    let decompressedSize = decompressedData.withUnsafeMutableBytes { destBuffer in
        compressedData.withUnsafeBytes { sourceBuffer in
            compression_decode_buffer(
                destBuffer.baseAddress!,
                expectedDecompressedSize,
                sourceBuffer.baseAddress!,
                compressedData.count,
                nil,
                COMPRESSION_ZLIB
            )
        }
    }

    guard decompressedSize == expectedDecompressedSize else {
        print("ERROR: Decompression failed. Expected \(expectedDecompressedSize), got \(decompressedSize)")
        return nil
    }

    // Decompress delta-encoded data back to full 24-bit samples
    var samples: [Int32] = []
    samples.reserveCapacity(2376)
    var prevSample: Int32 = 0

    for i in 0..<2376 {
        // Read 16-bit delta
        let deltaOffset = i * 2
        let delta = decompressedData.withUnsafeBytes { buffer in
            Int16(littleEndian: buffer.load(fromByteOffset: deltaOffset, as: Int16.self))
        }

        // Reconstruct sample
        prevSample += Int32(delta)
        samples.append(prevSample)
    }

    return samples
}

func calculateCRC32(samples: [Int32]) -> UInt32 {
    var crc: UInt32 = 0xFFFFFFFF

    // Calculate CRC on packed 24-bit format
    for sample in samples {
        let bytes = [
            UInt8(sample & 0xFF),
            UInt8((sample >> 8) & 0xFF),
            UInt8((sample >> 16) & 0xFF)
        ]

        for byte in bytes {
            crc ^= UInt32(byte)
            for _ in 0..<8 {
                if (crc & 1) != 0 {
                    crc = (crc >> 1) ^ 0xEDB88320
                } else {
                    crc >>= 1
                }
            }
        }
    }

    return ~crc
}

// Overloaded version for raw byte data
func calculateCRC32(data: Data) -> UInt32 {
    var crc: UInt32 = 0xFFFFFFFF

    // Calculate CRC directly on raw bytes (no unpacking/repacking)
    for byte in data {
        crc ^= UInt32(byte)
        for _ in 0..<8 {
            if (crc & 1) != 0 {
                crc = (crc >> 1) ^ 0xEDB88320
            } else {
                crc >>= 1
            }
        }
    }

    return ~crc
}

class BLETransferManager: NSObject, ObservableObject {
    // Published properties for UI
    @Published var isConnected = false
    @Published var connectionStateText = "Not connected"
    @Published var connectionStateColor: Color = .red
    @Published var deviceName = ""
    @Published var negotiatedMTU: Int = 23
    @Published var isTransferActive = false
    @Published var blocksReceived = 0
    @Published var totalBlocks = TOTAL_BLOCKS
    @Published var progressPercent: Double = 0.0
    @Published var throughputKBps: Double = 0.0
    @Published var dataMB: Double = 0.0
    @Published var elapsedSeconds: Double = 0.0
    @Published var elapsedTimeString = "0s"
    @Published var estimatedTotalTimeString = "--"
    @Published var estimatedRemainingTimeString = "--"
    @Published var currentWaveform: WaveformData?
    @Published var currentMode: String = "Waiting..."
    @Published var waveformFlashTrigger: UUID = UUID()

    // Core Bluetooth
    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var dataBlockCharacteristic: CBCharacteristic?
    private var controlCharacteristic: CBCharacteristic?

    // Transfer state
    private var currentBlock: UInt16 = 0
    private var receivedBlocks: [UInt16: Data] = [:]
    private var lastAckedBlock: UInt16 = 0
    private var blockChunks: [UInt16: [UInt16: Data]] = [:]
    private var blockExpectedChunks: [UInt16: UInt16] = [:]  // Track expected chunks per block
    private var lastChunkTime: Date?

    // Statistics
    private var startTime: Date?
    private var endTime: Date?
    private var totalBytesReceived: Int = 0
    private var totalChunksReceived: Int = 0
    private var statusTimer: Timer?
    private var uiUpdateTimer: Timer?
    private var stuckBlockTimer: Timer?

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }

    func startScanning() {
        print("Scanning for '\(DEVICE_NAME)'...")
        centralManager.scanForPeripherals(withServices: nil, options: nil)
        connectionStateText = "Scanning..."
        connectionStateColor = .orange
    }

    func startTransfer() {
        guard let characteristic = controlCharacteristic,
              let peripheral = peripheral else {
            print("ERROR: Control characteristic not ready")
            return
        }

        // Get actual negotiated MTU
        let maxWriteLength = peripheral.maximumWriteValueLength(for: .withoutResponse)
        negotiatedMTU = maxWriteLength + 3
        print("MTU: \(negotiatedMTU) bytes")

        // Create control message
        var data = Data()
        data.append(CMD_START)
        data.append(contentsOf: withUnsafeBytes(of: UInt16(0).littleEndian) { Data($0) })
        data.append(contentsOf: withUnsafeBytes(of: UInt32(Date().timeIntervalSince1970).littleEndian) { Data($0) })

        peripheral.writeValue(data, for: characteristic, type: .withoutResponse)

        isTransferActive = true
        startTime = Date()
        currentBlock = 0
        receivedBlocks.removeAll()
        blockChunks.removeAll()
        totalBytesReceived = 0
        totalChunksReceived = 0
        blocksReceived = 0
        progressPercent = 0.0

        print("\n========================================")
        print("TRANSFER STARTED")
        print("========================================\n")

        // Start UI update timer
        uiUpdateTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            self?.updateUIStats()
        }

        // Start status reporting timer
        statusTimer = Timer.scheduledTimer(withTimeInterval: 10.0, repeats: true) { [weak self] _ in
            self?.printStatus()
        }

        // Start stuck block detection timer
        stuckBlockTimer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: true) { [weak self] _ in
            self?.checkForStuckBlocks()
        }
    }

    private func checkForStuckBlocks() {
        guard let lastTime = lastChunkTime else { return }

        let timeSinceLastChunk = Date().timeIntervalSince(lastTime)

        if timeSinceLastChunk > 15.0 {
            print("WARNING: Stalled for \(Int(timeSinceLastChunk))s. Incomplete blocks: \(blockChunks.keys.sorted())")

            // Report missing chunks for first stuck block
            if let firstBlock = blockChunks.keys.sorted().first,
               let expectedChunks = blockExpectedChunks[firstBlock],
               let receivedChunks = blockChunks[firstBlock] {
                let missingChunks = (0..<expectedChunks).filter { !receivedChunks.keys.contains($0) }
                print("  Block \(firstBlock): received \(receivedChunks.count)/\(expectedChunks), missing chunks: \(missingChunks)")
            }
        }
    }

    func stopTransfer() {
        guard let characteristic = controlCharacteristic,
              let peripheral = peripheral else {
            return
        }

        var data = Data()
        data.append(CMD_STOP)
        data.append(contentsOf: withUnsafeBytes(of: UInt16(0).littleEndian) { Data($0) })
        data.append(contentsOf: withUnsafeBytes(of: UInt32(Date().timeIntervalSince1970).littleEndian) { Data($0) })

        peripheral.writeValue(data, for: characteristic, type: .withoutResponse)

        isTransferActive = false
        statusTimer?.invalidate()
        uiUpdateTimer?.invalidate()
        print("Transfer stopped by user")
    }

    private func updateUIStats() {
        guard let startTime = startTime else { return }

        let elapsed = Date().timeIntervalSince(startTime)
        elapsedSeconds = elapsed

        let minutes = Int(elapsed) / 60
        let seconds = elapsed - Double(minutes * 60)
        if minutes > 0 {
            elapsedTimeString = String(format: "%dm %.1fs", minutes, seconds)
        } else {
            elapsedTimeString = String(format: "%.1fs", elapsed)
        }

        throughputKBps = elapsed > 0 ? Double(totalBytesReceived) / elapsed / 1000.0 : 0.0
        dataMB = Double(totalBytesReceived) / 1024.0 / 1024.0
        progressPercent = Double(blocksReceived) * 100.0 / Double(TOTAL_BLOCKS)

        // Calculate estimated times
        if throughputKBps > 0 && blocksReceived > 0 {
            let totalBytes = Double(TOTAL_BLOCKS * BLOCK_SIZE)
            let remainingBytes = Double((TOTAL_BLOCKS - blocksReceived) * BLOCK_SIZE)

            // Estimated total time
            let estimatedTotalSeconds = totalBytes / (throughputKBps * 1000.0)
            let totalMins = Int(estimatedTotalSeconds) / 60
            let totalSecs = Int(estimatedTotalSeconds) % 60
            estimatedTotalTimeString = String(format: "%dm %ds", totalMins, totalSecs)

            // Estimated remaining time
            let estimatedRemainingSeconds = remainingBytes / (throughputKBps * 1000.0)
            let remainingMins = Int(estimatedRemainingSeconds) / 60
            let remainingSecs = Int(estimatedRemainingSeconds) % 60
            estimatedRemainingTimeString = String(format: "%dm %ds", remainingMins, remainingSecs)
        } else {
            estimatedTotalTimeString = "--"
            estimatedRemainingTimeString = "--"
        }
    }

    private func printStatus() {
        guard startTime != nil else { return }

        print("Blocks: \(blocksReceived)/\(TOTAL_BLOCKS) | Rate: \(String(format: "%.1f", throughputKBps)) KB/s")
    }

    func processChunk(_ data: Data) {
        guard data.count >= 12 else {
            print("WARNING: Invalid chunk size: \(data.count)")
            return
        }

        // Parse header (data is already a defensive copy from BLE callback)
        let blockNumber = UInt16(data[0]) | (UInt16(data[1]) << 8)
        let chunkNumber = UInt16(data[2]) | (UInt16(data[3]) << 8)
        let chunkSize = UInt16(data[4]) | (UInt16(data[5]) << 8)
        let totalChunks = UInt16(data[6]) | (UInt16(data[7]) << 8)

        // Debug first few chunks
        if blockNumber < 3 && chunkNumber < 5 {
            print("← Chunk B\(blockNumber) C\(chunkNumber)/\(totalChunks-1) size=\(chunkSize) packet=\(data.count)")
        }

        // Validate block number (sanity check)
        guard blockNumber < TOTAL_BLOCKS else {
            let headerBytes = data.prefix(12).map { String(format: "%02X", $0) }.joined(separator: " ")
            print("ERROR: Invalid block number \(blockNumber) (chunk \(chunkNumber)) - packet size: \(data.count)")
            print("  Header bytes: \(headerBytes)")
            return
        }

        // Extract chunk data
        let chunkData = data.subdata(in: 12..<min(data.count, 12 + Int(chunkSize)))

        // Store chunk
        if blockChunks[blockNumber] == nil {
            blockChunks[blockNumber] = [:]
            blockExpectedChunks[blockNumber] = totalChunks  // Remember expected count
        }
        blockChunks[blockNumber]![chunkNumber] = chunkData

        totalChunksReceived += 1
        totalBytesReceived += chunkData.count
        lastChunkTime = Date()

        // Check if block is complete
        if blockChunks[blockNumber]!.count == Int(totalChunks) {
            // Reassemble block
            var blockData = Data()
            for i in 0..<totalChunks {
                if let chunk = blockChunks[blockNumber]![i] {
                    blockData.append(chunk)
                }
            }

            if blockNumber < 3 {
                print("DEBUG: Block \(blockNumber) reassembled: \(blockData.count) bytes from \(totalChunks) chunks")
            }

            // Process uncompressed block (skip validation for speed)
            handleUncompressedBlock(blockNumber: blockNumber, blockData: blockData)

            // Mark complete
            receivedBlocks[blockNumber] = blockData
            blocksReceived = receivedBlocks.count
            blockChunks[blockNumber] = nil
            blockExpectedChunks[blockNumber] = nil

            // Send ACK
            let shouldAck = blockNumber > 0 && (blockNumber + 1) % UInt16(ACK_INTERVAL) == 0
            if shouldAck {
                sendAck(forBlock: blockNumber)
            }

            // Check if transfer is complete
            if receivedBlocks.count == TOTAL_BLOCKS {
                transferComplete()
            }
        }
    }

    private func handleCompressedBlock(blockNumber: UInt16, blockData: Data) -> Bool {
        guard blockData.count >= 38 else {
            print("ERROR: Block \(blockNumber) too small for waveform header")
            return false
        }

        // Parse waveform header
        let header = parseWaveformHeader(from: blockData)

        // Extract compressed data (header is 38 bytes, not 40!)
        let compressedData = blockData.subdata(in: 38..<blockData.count)

        // Decompress
        guard let samples = decompressWaveform(compressedData: compressedData) else {
            print("ERROR: Failed to decompress block \(blockNumber)")
            return false
        }

        // Verify CRC
        let calculatedCrc = calculateCRC32(samples: samples)
        if calculatedCrc != header.crc32 {
            print("ERROR: CRC mismatch for block \(blockNumber)!")
            return false
        }

        // Update displayed waveform (show latest)
        DispatchQueue.main.async {
            self.currentMode = "Compressed"
            self.currentWaveform = WaveformData(
                samples: samples,
                header: WaveformHeaderData(
                    blockNumber: header.blockNumber,
                    timestampMs: header.timestampMs,
                    sampleRateHz: header.sampleRateHz,
                    sampleCount: header.sampleCount,
                    triggerSample: header.triggerSample,
                    pulseFreqHz: header.pulseFreqHz,
                    temperatureCx10: header.temperatureCx10,
                    gainDb: header.gainDb
                )
            )
            // Trigger flash animation
            self.waveformFlashTrigger = UUID()
        }

        if blockNumber < 5 {
            let compressionRatio = 100.0 * (1.0 - Float(blockData.count) / 7168.0)
            print("Block \(blockNumber): Decompressed \(blockData.count) → \(samples.count * 3) bytes (CRC OK, \(String(format: "%.1f%%", compressionRatio)) compression)")
        }

        return true
    }

    private func handleUncompressedBlock(blockNumber: UInt16, blockData: Data) {
        if blockNumber < 3 {
            print("DEBUG: handleUncompressedBlock called for block \(blockNumber), size: \(blockData.count) bytes")
        }

        // Validate minimum size: 38 byte header + at least some sample data
        guard blockData.count >= 38 + 7128 else {
            print("WARNING: Block \(blockNumber) too small: \(blockData.count) bytes (expected >= \(38 + 7128))")
            return
        }

        // Parse waveform header
        let header = parseWaveformHeader(from: blockData)

        // Extract raw 24-bit sample data (header is 38 bytes)
        let sampleData = blockData.subdata(in: 38..<blockData.count)

        // Ensure we have exactly the expected amount of sample data
        guard sampleData.count >= 7128 else {
            print("WARNING: Block \(blockNumber) insufficient sample data: \(sampleData.count) bytes (expected >= 7128)")
            return
        }

        // Unpack 24-bit samples to Int32 array for display
        var samples: [Int32] = []
        samples.reserveCapacity(2376)

        for i in 0..<2376 {
            let offset = i * 3
            let byte0 = Int32(sampleData[offset])
            let byte1 = Int32(sampleData[offset + 1])
            let byte2 = Int32(sampleData[offset + 2])

            var sample = byte0 | (byte1 << 8) | (byte2 << 16)

            // Sign extend from 24-bit to 32-bit
            if sample & 0x800000 != 0 {
                sample |= Int32(bitPattern: 0xFF000000)
            }

            samples.append(sample)
        }

        // Update displayed waveform on every block
        DispatchQueue.main.async {
            self.currentMode = "Uncompressed"
            self.currentWaveform = WaveformData(
                samples: samples,
                header: WaveformHeaderData(
                    blockNumber: header.blockNumber,
                    timestampMs: header.timestampMs,
                    sampleRateHz: header.sampleRateHz,
                    sampleCount: header.sampleCount,
                    triggerSample: header.triggerSample,
                    pulseFreqHz: header.pulseFreqHz,
                    temperatureCx10: header.temperatureCx10,
                    gainDb: header.gainDb
                )
            )
            // Trigger flash animation on every block
            self.waveformFlashTrigger = UUID()
        }
    }

    private func parseWaveformHeader(from data: Data) -> (blockNumber: UInt32, timestampMs: UInt32, sampleRateHz: UInt32, sampleCount: UInt16, triggerSample: UInt16, pulseFreqHz: UInt32, temperatureCx10: Int16, gainDb: UInt8, crc32: UInt32) {
        // Manually read bytes to avoid alignment issues with packed struct
        let blockNumber = UInt32(data[0]) | (UInt32(data[1]) << 8) | (UInt32(data[2]) << 16) | (UInt32(data[3]) << 24)
        let timestampMs = UInt32(data[4]) | (UInt32(data[5]) << 8) | (UInt32(data[6]) << 16) | (UInt32(data[7]) << 24)
        let sampleRateHz = UInt32(data[8]) | (UInt32(data[9]) << 8) | (UInt32(data[10]) << 16) | (UInt32(data[11]) << 24)
        let sampleCount = UInt16(data[12]) | (UInt16(data[13]) << 8)
        let triggerSample = UInt16(data[16]) | (UInt16(data[17]) << 8)
        let pulseFreqHz = UInt32(data[18]) | (UInt32(data[19]) << 8) | (UInt32(data[20]) << 16) | (UInt32(data[21]) << 24)
        let temperatureCx10 = Int16(bitPattern: UInt16(data[26]) | (UInt16(data[27]) << 8))
        let gainDb = data[28]
        let crc32 = UInt32(data[30]) | (UInt32(data[31]) << 8) | (UInt32(data[32]) << 16) | (UInt32(data[33]) << 24)

        return (blockNumber: blockNumber, timestampMs: timestampMs, sampleRateHz: sampleRateHz,
                sampleCount: sampleCount, triggerSample: triggerSample, pulseFreqHz: pulseFreqHz,
                temperatureCx10: temperatureCx10, gainDb: gainDb, crc32: crc32)
    }

    private func sendAck(forBlock blockNumber: UInt16) {
        guard let characteristic = controlCharacteristic,
              let peripheral = peripheral else {
            return
        }

        var data = Data()
        data.append(CMD_ACK)
        data.append(contentsOf: withUnsafeBytes(of: blockNumber.littleEndian) { Data($0) })
        data.append(contentsOf: withUnsafeBytes(of: UInt32(Date().timeIntervalSince1970).littleEndian) { Data($0) })

        peripheral.writeValue(data, for: characteristic, type: .withoutResponse)
        lastAckedBlock = blockNumber
    }

    private func transferComplete() {
        isTransferActive = false
        endTime = Date()
        statusTimer?.invalidate()
        uiUpdateTimer?.invalidate()

        let elapsed = endTime!.timeIntervalSince(startTime ?? Date())
        let minutes = Int(elapsed) / 60
        let seconds = elapsed - Double(minutes * 60)

        print("\n========================================")
        print("TRANSFER COMPLETE")
        print("========================================")
        print("Duration: \(minutes)m \(String(format: "%.2f", seconds))s")
        print("Blocks: \(blocksReceived) / \(TOTAL_BLOCKS)")
        print("Data: \(String(format: "%.2f", dataMB)) MB")
        print("Rate: \(String(format: "%.2f", throughputKBps)) KB/s")
        print("========================================\n")

        if let lastBlock = receivedBlocks.keys.max() {
            sendAck(forBlock: lastBlock)
        }
    }
}

// MARK: - CBCentralManagerDelegate
extension BLETransferManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            print("Bluetooth powered on")
            connectionStateText = "Ready to scan"
            connectionStateColor = .yellow
        case .poweredOff:
            print("ERROR: Bluetooth is powered off")
            connectionStateText = "Bluetooth off"
            connectionStateColor = .red
        case .unauthorized:
            print("ERROR: Bluetooth unauthorized")
            connectionStateText = "Unauthorized"
            connectionStateColor = .red
        case .unsupported:
            print("ERROR: Bluetooth not supported")
            connectionStateText = "Not supported"
            connectionStateColor = .red
        default:
            connectionStateText = "Unknown state"
            connectionStateColor = .gray
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                       advertisementData: [String : Any], rssi RSSI: NSNumber) {
        guard let name = peripheral.name, name == DEVICE_NAME else {
            return
        }

        print("Found device: \(name)")
        self.peripheral = peripheral
        self.deviceName = name
        peripheral.delegate = self

        centralManager.stopScan()
        centralManager.connect(peripheral, options: nil)
        connectionStateText = "Connecting..."
        connectionStateColor = .orange
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        print("Connected")
        isConnected = true
        connectionStateText = "Connected"
        connectionStateColor = .green
        peripheral.discoverServices([SERVICE_UUID])
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        print("Disconnected")
        isConnected = false
        connectionStateText = "Disconnected"
        connectionStateColor = .red

        if isTransferActive {
            print("WARNING: Transfer interrupted - attempting reconnect...")
            centralManager.connect(peripheral, options: nil)
        }
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        print("ERROR: Failed to connect")
        connectionStateText = "Connection failed"
        connectionStateColor = .red
    }
}

// MARK: - CBPeripheralDelegate
extension BLETransferManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error = error {
            print("ERROR: Error discovering services: \(error.localizedDescription)")
            return
        }

        guard let services = peripheral.services else { return }

        for service in services {
            if service.uuid == SERVICE_UUID {
                print("Found Data Transfer Service")
                peripheral.discoverCharacteristics([DATA_BLOCK_UUID, CONTROL_UUID], for: service)
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error = error {
            print("ERROR: Error discovering characteristics: \(error.localizedDescription)")
            return
        }

        guard let characteristics = service.characteristics else { return }

        for characteristic in characteristics {
            switch characteristic.uuid {
            case DATA_BLOCK_UUID:
                print("Found Data Block characteristic")
                dataBlockCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)

            case CONTROL_UUID:
                print("Found Control characteristic")
                controlCharacteristic = characteristic

            default:
                break
            }
        }

        // Ready to start if we have both characteristics
        if dataBlockCharacteristic != nil && controlCharacteristic != nil {
            print("Ready to start transfer")
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if let error = error {
            print("ERROR: Error updating notification state: \(error.localizedDescription)")
            return
        }

        if characteristic.uuid == DATA_BLOCK_UUID {
            if characteristic.isNotifying {
                print("Subscribed to Data Block notifications")
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        // CRITICAL: Copy data IMMEDIATELY before any other code
        // CoreBluetooth can reuse buffers at any time
        guard let data = characteristic.value else { return }

        // Force immediate deep copy by allocating new buffer and memcpy
        var dataCopy = Data(count: data.count)
        dataCopy.withUnsafeMutableBytes { destBytes in
            data.withUnsafeBytes { srcBytes in
                destBytes.copyMemory(from: srcBytes)
            }
        }

        // Now safe to do other checks
        if let error = error {
            print("ERROR: Error receiving data: \(error.localizedDescription)")
            return
        }

        guard characteristic.uuid == DATA_BLOCK_UUID else {
            return
        }

        // Process the independent copy
        processChunk(dataCopy)
    }
}
