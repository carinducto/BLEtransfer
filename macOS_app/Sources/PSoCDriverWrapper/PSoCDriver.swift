import Foundation
import SwiftUI

// Swift wrapper for PSoC Driver C library

public struct PSoCProtocol {
    public static let serviceUUID = "A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D"
    public static let dataBlockUUID = "A1B2C3D5-E5F6-4A5B-8C9D-0E1F2A3B4C5D"
    public static let controlUUID = "A1B2C3D6-E5F6-4A5B-8C9D-0E1F2A3B4C5D"
    public static let deviceName = "Inductosense Temp"
    public static let totalBlocks: Int32 = 1800
    public static let blockSize: Int32 = 7168
    public static let ackInterval: Int32 = 20
}

public struct PSoCWaveformHeader {
    public let blockNumber: UInt32
    public let timestampMs: UInt32
    public let sampleRateHz: UInt32
    public let sampleCount: UInt16
    public let triggerSample: UInt16
    public let pulseFreqHz: UInt32
    public let temperatureCx10: Int16
    public let gainDb: UInt8

    init(from cHeader: waveform_header_t) {
        self.blockNumber = cHeader.block_number
        self.timestampMs = cHeader.timestamp_ms
        self.sampleRateHz = cHeader.sample_rate_hz
        self.sampleCount = cHeader.sample_count
        self.triggerSample = cHeader.trigger_sample
        self.pulseFreqHz = cHeader.pulse_freq_hz
        self.temperatureCx10 = cHeader.temperature_cx10
        self.gainDb = cHeader.gain_db
    }
}

public struct PSoCWaveform {
    public let header: PSoCWaveformHeader
    public let samples: [Int32]
    public let isCompressed: Bool

    init(from cWaveform: waveform_data_t, isCompressed: Bool) {
        self.header = PSoCWaveformHeader(from: cWaveform.header)
        var mutableWaveform = cWaveform
        self.samples = withUnsafeBytes(of: &mutableWaveform.samples) { bytes in
            let buffer = bytes.bindMemory(to: Int32.self)
            return Array(buffer)
        }
        self.isCompressed = isCompressed
    }
}

public struct PSoCTransferStats {
    public let blocksReceived: UInt32
    public let totalBlocks: UInt32
    public let totalBytesReceived: UInt32
    public let totalChunksReceived: UInt32
    public let throughputKBps: Double
    public let progressPercent: Double
    public let elapsedSeconds: Double

    init(from cStats: transfer_stats_t) {
        self.blocksReceived = cStats.blocks_received
        self.totalBlocks = cStats.total_blocks
        self.totalBytesReceived = cStats.total_bytes_received
        self.totalChunksReceived = cStats.total_chunks_received
        self.throughputKBps = cStats.throughput_kbps
        self.progressPercent = cStats.progress_percent
        self.elapsedSeconds = cStats.elapsed_seconds
    }
}

public class PSoCTransferSession {
    private var session: OpaquePointer?
    public var onWaveform: ((PSoCWaveform) -> Void)?
    public var onProgress: ((PSoCTransferStats) -> Void)?
    public var onCompletion: ((PSoCTransferStats) -> Void)?
    public var onAck: ((UInt16) -> Void)?

    public init() {
        session = transfer_session_create()
        setupCallbacks()
    }

    deinit {
        if let session = session {
            transfer_session_destroy(session)
        }
    }

    private func setupCallbacks() {
        guard let session = session else { return }

        // Waveform callback
        let waveformContext = Unmanaged.passUnretained(self).toOpaque()
        transfer_session_set_waveform_callback(session, { waveformPtr, isCompressed, userData in
            guard let waveformPtr = waveformPtr, let userData = userData else { return }
            let selfRef = Unmanaged<PSoCTransferSession>.fromOpaque(userData).takeUnretainedValue()
            let waveform = waveformPtr.pointee
            let swiftWaveform = PSoCWaveform(from: waveform, isCompressed: isCompressed)
            DispatchQueue.main.async {
                selfRef.onWaveform?(swiftWaveform)
            }
        }, waveformContext)

        // Progress callback
        let progressContext = Unmanaged.passUnretained(self).toOpaque()
        transfer_session_set_progress_callback(session, { statsPtr, userData in
            guard let statsPtr = statsPtr, let userData = userData else { return }
            let selfRef = Unmanaged<PSoCTransferSession>.fromOpaque(userData).takeUnretainedValue()
            let stats = PSoCTransferStats(from: statsPtr.pointee)
            DispatchQueue.main.async {
                selfRef.onProgress?(stats)
            }
        }, progressContext)

        // Completion callback
        let completionContext = Unmanaged.passUnretained(self).toOpaque()
        transfer_session_set_completion_callback(session, { statsPtr, userData in
            guard let statsPtr = statsPtr, let userData = userData else { return }
            let selfRef = Unmanaged<PSoCTransferSession>.fromOpaque(userData).takeUnretainedValue()
            let stats = PSoCTransferStats(from: statsPtr.pointee)
            DispatchQueue.main.async {
                selfRef.onCompletion?(stats)
            }
        }, completionContext)

        // ACK callback
        let ackContext = Unmanaged.passUnretained(self).toOpaque()
        transfer_session_set_ack_callback(session, { blockNumber, userData in
            guard let userData = userData else { return }
            let selfRef = Unmanaged<PSoCTransferSession>.fromOpaque(userData).takeUnretainedValue()
            selfRef.onAck?(blockNumber)
        }, ackContext)
    }

    public func start() {
        guard let session = session else { return }
        transfer_session_start(session)
    }

    public func stop() {
        guard let session = session else { return }
        transfer_session_stop(session)
    }

    public func processChunk(data: Data) -> Bool {
        guard let session = session else { return false }
        return data.withUnsafeBytes { bytes in
            guard let baseAddress = bytes.baseAddress else { return false }
            return transfer_session_process_chunk(session, baseAddress.assumingMemoryBound(to: UInt8.self), bytes.count)
        }
    }

    public func getStats() -> PSoCTransferStats? {
        guard let session = session else { return nil }
        var stats = transfer_stats_t()
        transfer_session_get_stats(session, &stats)
        return PSoCTransferStats(from: stats)
    }

    public var isActive: Bool {
        guard let session = session else { return false }
        return transfer_session_is_active(session)
    }
}
