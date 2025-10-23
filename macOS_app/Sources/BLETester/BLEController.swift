import Foundation
import CoreBluetooth
import SwiftUI

// BLE Controller - Layer 2: BLE Interface Control
// Handles CoreBluetooth operations and delegates protocol logic to PSoC Driver (Layer 3)

class BLEController: NSObject, ObservableObject {
    // Published properties for UI
    @Published var isConnected = false
    @Published var connectionStateText = "Not connected"
    @Published var connectionStateColor: Color = .red
    @Published var deviceName = ""
    @Published var negotiatedMTU: Int = 23
    @Published var isTransferActive = false
    @Published var currentWaveform: PSoCWaveform?
    @Published var currentMode: String = "Waiting..."
    @Published var waveformFlashTrigger: UUID = UUID()
    @Published var transferStats: PSoCTransferStats?

    // Core Bluetooth
    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var dataBlockCharacteristic: CBCharacteristic?
    private var controlCharacteristic: CBCharacteristic?

    // PSoC Driver Session
    private var transferSession: PSoCTransferSession?

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
        setupTransferSession()
    }

    private func setupTransferSession() {
        transferSession = PSoCTransferSession()

        // Waveform callback
        transferSession?.onWaveform = { [weak self] waveform in
            guard let self = self else { return }
            self.currentWaveform = waveform
            self.currentMode = waveform.isCompressed ? "Compressed" : "Uncompressed"
            self.waveformFlashTrigger = UUID()
        }

        // Progress callback
        transferSession?.onProgress = { [weak self] stats in
            guard let self = self else { return }
            self.transferStats = stats
        }

        // Completion callback
        transferSession?.onCompletion = { [weak self] stats in
            guard let self = self else { return }
            self.isTransferActive = false
            self.transferStats = stats
            print("Transfer complete!")
            print("Blocks: \(stats.blocksReceived)/\(stats.totalBlocks)")
            print("Data: \(String(format: "%.2f MB", Double(stats.totalBytesReceived) / 1024.0 / 1024.0))")
            print("Rate: \(String(format: "%.2f KB/s", stats.throughputKBps))")
        }

        // ACK callback
        transferSession?.onAck = { [weak self] blockNumber in
            guard let self = self else { return }
            self.sendAck(forBlock: blockNumber)
        }
    }

    func startScanning() {
        print("Scanning for '\(PSoCProtocol.deviceName)'...")
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

        // Create start command
        var data = Data()
        data.append(0x01)  // CMD_START
        data.append(contentsOf: withUnsafeBytes(of: UInt16(0).littleEndian) { Data($0) })
        data.append(contentsOf: withUnsafeBytes(of: UInt32(Date().timeIntervalSince1970).littleEndian) { Data($0) })

        peripheral.writeValue(data, for: characteristic, type: .withoutResponse)

        // Start transfer session
        transferSession?.start()
        isTransferActive = true

        print("\n========================================")
        print("TRANSFER STARTED")
        print("========================================\n")
    }

    func stopTransfer() {
        guard let characteristic = controlCharacteristic,
              let peripheral = peripheral else {
            return
        }

        // Create stop command
        var data = Data()
        data.append(0x02)  // CMD_STOP
        data.append(contentsOf: withUnsafeBytes(of: UInt16(0).littleEndian) { Data($0) })
        data.append(contentsOf: withUnsafeBytes(of: UInt32(Date().timeIntervalSince1970).littleEndian) { Data($0) })

        peripheral.writeValue(data, for: characteristic, type: .withoutResponse)

        // Stop transfer session
        transferSession?.stop()
        isTransferActive = false
        print("Transfer stopped by user")
    }

    private func sendAck(forBlock blockNumber: UInt16) {
        guard let characteristic = controlCharacteristic,
              let peripheral = peripheral else {
            return
        }

        var data = Data()
        data.append(0x03)  // CMD_ACK
        data.append(contentsOf: withUnsafeBytes(of: blockNumber.littleEndian) { Data($0) })
        data.append(contentsOf: withUnsafeBytes(of: UInt32(Date().timeIntervalSince1970).littleEndian) { Data($0) })

        peripheral.writeValue(data, for: characteristic, type: .withoutResponse)
    }
}

// MARK: - CBCentralManagerDelegate
extension BLEController: CBCentralManagerDelegate {
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
        guard let name = peripheral.name, name == PSoCProtocol.deviceName else {
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

        let serviceUUID = CBUUID(string: PSoCProtocol.serviceUUID)
        peripheral.discoverServices([serviceUUID])
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
extension BLEController: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error = error {
            print("ERROR: Error discovering services: \(error.localizedDescription)")
            return
        }

        guard let services = peripheral.services else { return }

        let serviceUUID = CBUUID(string: PSoCProtocol.serviceUUID)
        let dataBlockUUID = CBUUID(string: PSoCProtocol.dataBlockUUID)
        let controlUUID = CBUUID(string: PSoCProtocol.controlUUID)

        for service in services {
            if service.uuid == serviceUUID {
                print("Found Data Transfer Service")
                peripheral.discoverCharacteristics([dataBlockUUID, controlUUID], for: service)
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error = error {
            print("ERROR: Error discovering characteristics: \(error.localizedDescription)")
            return
        }

        guard let characteristics = service.characteristics else { return }

        let dataBlockUUID = CBUUID(string: PSoCProtocol.dataBlockUUID)
        let controlUUID = CBUUID(string: PSoCProtocol.controlUUID)

        for characteristic in characteristics {
            switch characteristic.uuid {
            case dataBlockUUID:
                print("Found Data Block characteristic")
                dataBlockCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)

            case controlUUID:
                print("Found Control characteristic")
                controlCharacteristic = characteristic

            default:
                break
            }
        }

        if dataBlockCharacteristic != nil && controlCharacteristic != nil {
            print("Ready to start transfer")
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if let error = error {
            print("ERROR: Error updating notification state: \(error.localizedDescription)")
            return
        }

        let dataBlockUUID = CBUUID(string: PSoCProtocol.dataBlockUUID)
        if characteristic.uuid == dataBlockUUID {
            if characteristic.isNotifying {
                print("Subscribed to Data Block notifications")
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard let data = characteristic.value else { return }

        if let error = error {
            print("ERROR: Error receiving data: \(error.localizedDescription)")
            return
        }

        let dataBlockUUID = CBUUID(string: PSoCProtocol.dataBlockUUID)
        guard characteristic.uuid == dataBlockUUID else {
            return
        }

        // Process chunk through driver
        _ = transferSession?.processChunk(data: data)
    }
}
