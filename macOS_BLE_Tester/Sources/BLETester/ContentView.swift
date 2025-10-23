import SwiftUI

struct ContentView: View {
    @StateObject private var bleManager = BLETransferManager()

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
                        Text(bleManager.currentMode)
                            .font(.caption)
                            .fontWeight(.semibold)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 4)
                            .background(modeColor(for: bleManager.currentMode))
                            .foregroundColor(.white)
                            .cornerRadius(4)
                    }
                    .padding(.horizontal)
                    .padding(.top)

                    if let waveform = bleManager.currentWaveform {
                        WaveformFlashView(
                            samples: waveform.samples,
                            header: waveform.header,
                            flashTrigger: bleManager.waveformFlashTrigger
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
                    .fill(bleManager.connectionStateColor)
                    .frame(width: 12, height: 12)
                Text(bleManager.connectionStateText)
                    .font(.body)
            }

            if bleManager.isConnected {
                Text("Device: \(bleManager.deviceName)")
                    .font(.caption)
                    .foregroundColor(.secondary)

                Text("MTU: \(bleManager.negotiatedMTU) bytes")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
    }

    private var transferStatsSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Transfer Progress")
                .font(.headline)

            if bleManager.isTransferActive {
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Text("Blocks:")
                        Spacer()
                        Text("\(bleManager.blocksReceived) / \(bleManager.totalBlocks)")
                            .fontWeight(.semibold)
                    }

                    ProgressView(value: Double(bleManager.blocksReceived),
                               total: Double(bleManager.totalBlocks))

                    HStack {
                        Text("Progress:")
                        Spacer()
                        Text(String(format: "%.1f%%", bleManager.progressPercent))
                            .fontWeight(.semibold)
                    }

                    HStack {
                        Text("Rate:")
                        Spacer()
                        Text(String(format: "%.1f KB/s", bleManager.throughputKBps))
                            .fontWeight(.semibold)
                    }

                    HStack {
                        Text("Data:")
                        Spacer()
                        Text(String(format: "%.2f MB", bleManager.dataMB))
                            .fontWeight(.semibold)
                    }

                    if bleManager.elapsedSeconds > 0 {
                        HStack {
                            Text("Elapsed:")
                            Spacer()
                            Text(bleManager.elapsedTimeString)
                                .fontWeight(.semibold)
                        }

                        HStack {
                            Text("Remaining:")
                            Spacer()
                            Text(bleManager.estimatedRemainingTimeString)
                                .fontWeight(.semibold)
                                .foregroundColor(.orange)
                        }

                        HStack {
                            Text("Est. Total:")
                            Spacer()
                            Text(bleManager.estimatedTotalTimeString)
                                .fontWeight(.semibold)
                                .foregroundColor(.secondary)
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
                if bleManager.isTransferActive {
                    bleManager.stopTransfer()
                } else {
                    bleManager.startTransfer()
                }
            }) {
                HStack {
                    Image(systemName: bleManager.isTransferActive ? "stop.fill" : "play.fill")
                    Text(bleManager.isTransferActive ? "Stop Transfer" : "Start Transfer")
                }
                .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .disabled(!bleManager.isConnected)

            Button(action: {
                bleManager.startScanning()
            }) {
                HStack {
                    Image(systemName: "antenna.radiowaves.left.and.right")
                    Text("Scan for Device")
                }
                .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .disabled(bleManager.isConnected)
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
}

// Wrapper view to add flash animation
struct WaveformFlashView: View {
    let samples: [Int32]
    let header: WaveformHeaderData
    let flashTrigger: UUID

    @State private var flashOpacity: Double = 1.0

    var body: some View {
        WaveformView(samples: samples, header: header)
            .opacity(flashOpacity)
            .onChange(of: flashTrigger) { _ in
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
