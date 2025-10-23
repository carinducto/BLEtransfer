import SwiftUI

struct WaveformData {
    let samples: [Int32]
    let header: WaveformHeaderData
}

struct WaveformHeaderData {
    let blockNumber: UInt32
    let timestampMs: UInt32
    let sampleRateHz: UInt32
    let sampleCount: UInt16
    let triggerSample: UInt16
    let pulseFreqHz: UInt32
    let temperatureCx10: Int16
    let gainDb: UInt8
}

struct WaveformView: View {
    let samples: [Int32]
    let header: WaveformHeaderData

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            // Waveform metadata
            waveformInfo

            // Waveform plot
            GeometryReader { geometry in
                Canvas { context, size in
                    drawWaveform(context: context, size: size)
                }
            }
            .background(Color(NSColor.textBackgroundColor))
            .cornerRadius(8)
            .overlay(
                RoundedRectangle(cornerRadius: 8)
                    .stroke(Color.gray.opacity(0.3), lineWidth: 1)
            )
        }
    }

    private var waveformInfo: some View {
        HStack(spacing: 24) {
            infoItem("Block", String(header.blockNumber))
            infoItem("Sample Rate", "\(header.sampleRateHz / 1_000_000) MHz")
            infoItem("Samples", String(header.sampleCount))
            infoItem("Carrier", "\(header.pulseFreqHz / 1_000_000) MHz")
            infoItem("Temp", String(format: "%.1f°C", Float(header.temperatureCx10) / 10.0))
            infoItem("Gain", "\(header.gainDb) dB")
        }
        .font(.caption)
        .padding(.horizontal)
    }

    private func infoItem(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label)
                .foregroundColor(.secondary)
                .font(.caption2)
            Text(value)
                .fontWeight(.semibold)
        }
    }

    private func drawWaveform(context: GraphicsContext, size: CGSize) {
        guard !samples.isEmpty else { return }

        // Calculate scaling
        let minSample = samples.min() ?? 0
        let maxSample = samples.max() ?? 1
        let range = Float(maxSample - minSample)
        guard range > 0 else { return }

        let margin: CGFloat = 40
        let plotWidth = size.width - 2 * margin
        let plotHeight = size.height - 2 * margin
        let centerY = margin + plotHeight / 2

        // Draw axes
        context.stroke(
            Path { path in
                // X axis
                path.move(to: CGPoint(x: margin, y: centerY))
                path.addLine(to: CGPoint(x: size.width - margin, y: centerY))

                // Y axis
                path.move(to: CGPoint(x: margin, y: margin))
                path.addLine(to: CGPoint(x: margin, y: size.height - margin))
            },
            with: .color(.gray.opacity(0.5)),
            lineWidth: 1
        )

        // Draw grid lines
        for i in 1...4 {
            let y = margin + (plotHeight * CGFloat(i)) / 5
            context.stroke(
                Path { path in
                    path.move(to: CGPoint(x: margin, y: y))
                    path.addLine(to: CGPoint(x: size.width - margin, y: y))
                },
                with: .color(.gray.opacity(0.2)),
                lineWidth: 0.5
            )
        }

        // Draw waveform
        var path = Path()
        for (index, sample) in samples.enumerated() {
            let x = margin + (plotWidth * CGFloat(index)) / CGFloat(samples.count - 1)
            let normalizedValue = (Float(sample - minSample) / range) - 0.5 // Center around 0
            let y = centerY - (CGFloat(normalizedValue) * plotHeight)

            if index == 0 {
                path.move(to: CGPoint(x: x, y: y))
            } else {
                path.addLine(to: CGPoint(x: x, y: y))
            }
        }

        context.stroke(
            path,
            with: .color(.blue),
            lineWidth: 1.0
        )

        // Draw trigger marker if available
        if header.triggerSample < samples.count {
            let triggerX = margin + (plotWidth * CGFloat(header.triggerSample)) / CGFloat(samples.count - 1)
            context.stroke(
                Path { path in
                    path.move(to: CGPoint(x: triggerX, y: margin))
                    path.addLine(to: CGPoint(x: triggerX, y: size.height - margin))
                },
                with: .color(.red.opacity(0.5)),
                style: StrokeStyle(lineWidth: 1.5, dash: [5, 5])
            )

            // Draw trigger label
            let triggerText = Text("Trigger")
                .font(.caption2)
                .foregroundColor(.red)
            context.draw(triggerText, at: CGPoint(x: triggerX + 5, y: margin + 10))
        }

        // Draw time scale labels
        let timeScalePoints = [0, samples.count / 4, samples.count / 2, samples.count * 3 / 4, samples.count - 1]
        for point in timeScalePoints {
            let x = margin + (plotWidth * CGFloat(point)) / CGFloat(samples.count - 1)
            let timeUs = Float(point) / Float(header.sampleRateHz) * 1_000_000
            let label = Text(String(format: "%.1f μs", timeUs))
                .font(.caption2)
                .foregroundColor(.secondary)
            context.draw(label, at: CGPoint(x: x, y: size.height - margin + 15))
        }

        // Draw amplitude scale labels
        let amplitudeLabels = [maxSample, maxSample / 2, 0, minSample / 2, minSample]
        for (i, amp) in amplitudeLabels.enumerated() {
            let y = margin + (plotHeight * CGFloat(i)) / 4
            let label = Text(String(format: "%.0fk", Float(amp) / 1000.0))
                .font(.caption2)
                .foregroundColor(.secondary)
            context.draw(label, at: CGPoint(x: margin - 20, y: y))
        }
    }
}
