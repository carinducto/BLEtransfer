using System;
using System.Runtime.InteropServices;
using System.Windows;

namespace BLETester.Interop
{
    /// <summary>
    /// Managed wrapper for waveform header
    /// </summary>
    public class WaveformHeader
    {
        public uint BlockNumber { get; set; }
        public uint TimestampMs { get; set; }
        public uint SampleRateHz { get; set; }
        public ushort SampleCount { get; set; }
        public ushort TriggerSample { get; set; }
        public uint PulseFreqHz { get; set; }
        public short TemperatureCx10 { get; set; }
        public byte GainDb { get; set; }

        internal static WaveformHeader FromNative(NativeMethods.WaveformHeader native)
        {
            return new WaveformHeader
            {
                BlockNumber = native.BlockNumber,
                TimestampMs = native.TimestampMs,
                SampleRateHz = native.SampleRateHz,
                SampleCount = native.SampleCount,
                TriggerSample = native.TriggerSample,
                PulseFreqHz = native.PulseFreqHz,
                TemperatureCx10 = native.TemperatureCx10,
                GainDb = native.GainDb
            };
        }
    }

    /// <summary>
    /// Managed wrapper for waveform data
    /// </summary>
    public class Waveform
    {
        public WaveformHeader Header { get; set; }
        public int[] Samples { get; set; }
        public bool IsCompressed { get; set; }

        public Waveform(WaveformHeader header, int[] samples, bool isCompressed)
        {
            Header = header;
            Samples = samples;
            IsCompressed = isCompressed;
        }
    }

    /// <summary>
    /// Managed wrapper for transfer statistics
    /// </summary>
    public class TransferStats
    {
        public uint BlocksReceived { get; set; }
        public uint TotalBlocks { get; set; }
        public uint TotalBytesReceived { get; set; }
        public uint TotalChunksReceived { get; set; }
        public double ThroughputKBps { get; set; }
        public double ProgressPercent { get; set; }
        public double ElapsedSeconds { get; set; }

        internal static TransferStats FromNative(NativeMethods.TransferStats native)
        {
            return new TransferStats
            {
                BlocksReceived = native.BlocksReceived,
                TotalBlocks = native.TotalBlocks,
                TotalBytesReceived = native.TotalBytesReceived,
                TotalChunksReceived = native.TotalChunksReceived,
                ThroughputKBps = native.ThroughputKBps,
                ProgressPercent = native.ProgressPercent,
                ElapsedSeconds = native.ElapsedSeconds
            };
        }
    }

    /// <summary>
    /// Managed wrapper for PSoC transfer session
    /// </summary>
    public class TransferSession : IDisposable
    {
        private IntPtr _session;
        private NativeMethods.WaveformCallback _waveformCallback;
        private NativeMethods.ProgressCallback _progressCallback;
        private NativeMethods.CompletionCallback _completionCallback;
        private NativeMethods.AckCallback _ackCallback;

        public event Action<Waveform> OnWaveform;
        public event Action<TransferStats> OnProgress;
        public event Action<TransferStats> OnCompletion;
        public event Action<ushort> OnAck;

        public TransferSession()
        {
            _session = NativeMethods.transfer_session_create();
            if (_session == IntPtr.Zero)
                throw new InvalidOperationException("Failed to create transfer session");

            SetupCallbacks();
        }

        private void SetupCallbacks()
        {
            // Store delegates as fields to prevent garbage collection
            _waveformCallback = (waveformPtr, isCompressed, userData) =>
            {
                try
                {
                    if (waveformPtr == IntPtr.Zero) return;

                    // Read header
                    var header = Marshal.PtrToStructure<NativeMethods.WaveformHeader>(waveformPtr);
                    var managedHeader = WaveformHeader.FromNative(header);

                    // Read samples (2376 int32 values after header)
                    var samplesPtr = IntPtr.Add(waveformPtr, Marshal.SizeOf<NativeMethods.WaveformHeader>());
                    var samples = new int[2376];
                    Marshal.Copy(samplesPtr, samples, 0, 2376);

                    var waveform = new Waveform(managedHeader, samples, isCompressed);

                    // Invoke on UI thread
                    Application.Current?.Dispatcher.Invoke(() => OnWaveform?.Invoke(waveform));
                }
                catch (Exception ex)
                {
                    System.Diagnostics.Debug.WriteLine($"Error in waveform callback: {ex.Message}");
                }
            };

            _progressCallback = (statsPtr, userData) =>
            {
                try
                {
                    if (statsPtr == IntPtr.Zero) return;

                    var stats = Marshal.PtrToStructure<NativeMethods.TransferStats>(statsPtr);
                    var managedStats = TransferStats.FromNative(stats);

                    Application.Current?.Dispatcher.Invoke(() => OnProgress?.Invoke(managedStats));
                }
                catch (Exception ex)
                {
                    System.Diagnostics.Debug.WriteLine($"Error in progress callback: {ex.Message}");
                }
            };

            _completionCallback = (statsPtr, userData) =>
            {
                try
                {
                    if (statsPtr == IntPtr.Zero) return;

                    var stats = Marshal.PtrToStructure<NativeMethods.TransferStats>(statsPtr);
                    var managedStats = TransferStats.FromNative(stats);

                    Application.Current?.Dispatcher.Invoke(() => OnCompletion?.Invoke(managedStats));
                }
                catch (Exception ex)
                {
                    System.Diagnostics.Debug.WriteLine($"Error in completion callback: {ex.Message}");
                }
            };

            _ackCallback = (blockNumber, userData) =>
            {
                OnAck?.Invoke(blockNumber);
            };

            // Register callbacks
            NativeMethods.transfer_session_set_waveform_callback(_session, _waveformCallback, IntPtr.Zero);
            NativeMethods.transfer_session_set_progress_callback(_session, _progressCallback, IntPtr.Zero);
            NativeMethods.transfer_session_set_completion_callback(_session, _completionCallback, IntPtr.Zero);
            NativeMethods.transfer_session_set_ack_callback(_session, _ackCallback, IntPtr.Zero);
        }

        public void Start()
        {
            NativeMethods.transfer_session_start(_session);
        }

        public void Stop()
        {
            NativeMethods.transfer_session_stop(_session);
        }

        public bool ProcessChunk(byte[] data)
        {
            return NativeMethods.transfer_session_process_chunk(_session, data, (UIntPtr)data.Length);
        }

        public TransferStats GetStats()
        {
            NativeMethods.transfer_session_get_stats(_session, out var stats);
            return TransferStats.FromNative(stats);
        }

        public bool IsActive
        {
            get { return NativeMethods.transfer_session_is_active(_session); }
        }

        public void Dispose()
        {
            if (_session != IntPtr.Zero)
            {
                NativeMethods.transfer_session_destroy(_session);
                _session = IntPtr.Zero;
            }
            GC.SuppressFinalize(this);
        }

        ~TransferSession()
        {
            Dispose();
        }
    }
}
