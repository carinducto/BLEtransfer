import SwiftUI

struct ContentView: View {
    @StateObject private var bleController = BLEController()

    var body: some View {
        VStack(spacing: 0) {
            // Header
            headerView

            Divider()

            // Main content area
            HStack(spacing: 0) {
                // Left panel - Status and controls
                VStack(alignment: .leading, spacing: 16) {
                    statusSection

                    Divider()

                    transferStatsSection

                    Spacer()

                    controlButtons
                }
                .frame(width: 300)
                .padding()
                .background(Color(NSColor.controlBackgroundColor))

                Divider()

                // Right panel - Waveform display
                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        Text("Ultrasound Waveform")
                            .font(.headline)

                        // Mode badge
                        Text(bleController.currentMode)
                            .font(.caption)
                            .fontWeight(.semibold)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 4)
                            .background(modeColor(for: bleController.currentMode))
                            .foregroundColor(.white)
                            .cornerRadius(4)
                    }
                    .padding(.horizontal)
                    .padding(.top)

                    if let waveform = bleController.currentWaveform {
                        WaveformFlashView(
                            waveform: waveform,
                            flashTrigger: bleController.waveformFlashTrigger
                        )
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .padding()
                    } else {
                        VStack {
                            Spacer()
                            Text("No waveform data yet")
                                .foregroundColor(.secondary)
                            Text("Start transfer to see waveforms")
                                .font(.caption)
                                .foregroundColor(.secondary)
                            Spacer()
                        }
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
    }

    private var headerView: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Inductosense RTC Data Transfer")
                .font(.title)
                .fontWeight(.bold)
            Text("PSoC6 BLE Ultrasound Waveform Receiver")
                .font(.subheadline)
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding()
        .background(Color(NSColor.windowBackgroundColor))
    }

    private var statusSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Connection Status")
                .font(.headline)

            HStack {
                Circle()
                    .fill(bleController.connectionStateColor)
                    .frame(width: 12, height: 12)
                Text(bleController.connectionStateText)
                    .font(.body)
            }

            if bleController.isConnected {
                Text("Device: \(bleController.deviceName)")
                    .font(.caption)
                    .foregroundColor(.secondary)

                Text("MTU: \(bleController.negotiatedMTU) bytes")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
    }

    private var transferStatsSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Transfer Progress")
                .font(.headline)

            if bleController.isTransferActive, let stats = bleController.transferStats {
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Text("Blocks:")
                        Spacer()
                        Text("\(stats.blocksReceived) / \(stats.totalBlocks)")
                            .fontWeight(.semibold)
                    }

                    ProgressView(value: Double(stats.blocksReceived),
                               total: Double(stats.totalBlocks))

                    HStack {
                        Text("Progress:")
                        Spacer()
                        Text(String(format: "%.1f%%", stats.progressPercent))
                            .fontWeight(.semibold)
                    }

                    HStack {
                        Text("Rate:")
                        Spacer()
                        Text(String(format: "%.1f KB/s", stats.throughputKBps))
                            .fontWeight(.semibold)
                    }

                    HStack {
                        Text("Data:")
                        Spacer()
                        Text(String(format: "%.2f MB", Double(stats.totalBytesReceived) / 1024.0 / 1024.0))
                            .fontWeight(.semibold)
                    }

                    if stats.elapsedSeconds > 0 {
                        HStack {
                            Text("Elapsed:")
                            Spacer()
                            Text(formatTime(stats.elapsedSeconds))
                                .fontWeight(.semibold)
                        }

                        if stats.blocksReceived > 0 {
                            HStack {
                                Text("Remaining:")
                                Spacer()
                                Text(formatTime(estimateRemaining(stats: stats)))
                                    .fontWeight(.semibold)
                                    .foregroundColor(.orange)
                            }

                            HStack {
                                Text("Est. Total:")
                                Spacer()
                                Text(formatTime(estimateTotal(stats: stats)))
                                    .fontWeight(.semibold)
                                    .foregroundColor(.secondary)
                            }
                        }
                    }
                }
                .font(.caption)
            } else {
                Text("No active transfer")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
    }

    private var controlButtons: some View {
        VStack(spacing: 12) {
            Button(action: {
                if bleController.isTransferActive {
                    bleController.stopTransfer()
                } else {
                    bleController.startTransfer()
                }
            }) {
                HStack {
                    Image(systemName: bleController.isTransferActive ? "stop.fill" : "play.fill")
                    Text(bleController.isTransferActive ? "Stop Transfer" : "Start Transfer")
                }
                .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .disabled(!bleController.isConnected)

            Button(action: {
                bleController.startScanning()
            }) {
                HStack {
                    Image(systemName: "antenna.radiowaves.left.and.right")
                    Text("Scan for Device")
                }
                .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .disabled(bleController.isConnected)
        }
    }

    private func modeColor(for mode: String) -> Color {
        switch mode {
        case "Compressed":
            return .blue
        case "Uncompressed":
            return .orange
        default:
            return .gray
        }
    }

    private func formatTime(_ seconds: Double) -> String {
        let mins = Int(seconds) / 60
        let secs = seconds - Double(mins * 60)
        if mins > 0 {
            return String(format: "%dm %.1fs", mins, secs)
        } else {
            return String(format: "%.1fs", seconds)
        }
    }

    private func estimateRemaining(stats: PSoCTransferStats) -> Double {
        guard stats.throughputKBps > 0 else { return 0 }
        let remainingBytes = Double((stats.totalBlocks - stats.blocksReceived) * 7168)
        return remainingBytes / (stats.throughputKBps * 1000.0)
    }

    private func estimateTotal(stats: PSoCTransferStats) -> Double {
        guard stats.throughputKBps > 0 else { return 0 }
        let totalBytes = Double(stats.totalBlocks * 7168)
        return totalBytes / (stats.throughputKBps * 1000.0)
    }
}

// Wrapper view to add flash animation
struct WaveformFlashView: View {
    let waveform: PSoCWaveform
    let flashTrigger: UUID

    @State private var flashOpacity: Double = 1.0

    var body: some View {
        WaveformView(waveform: waveform)
            .opacity(flashOpacity)
            .onChange(of: flashTrigger) {
                // Flash animation: briefly dim then restore
                withAnimation(.easeOut(duration: 0.15)) {
                    flashOpacity = 0.3
                }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) {
                    withAnimation(.easeIn(duration: 0.15)) {
                        flashOpacity = 1.0
                    }
                }
            }
    }
}
