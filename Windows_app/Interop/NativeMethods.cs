using System;
using System.Runtime.InteropServices;

namespace BLETester.Interop
{
    /// <summary>
    /// P/Invoke declarations for the PSoC C++ driver library
    /// </summary>
    internal static class NativeMethods
    {
        private const string DllName = "psoc_driver.dll";

        // Protocol constants
        public const string ServiceUUID = "A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D";
        public const string DataBlockUUID = "A1B2C3D5-E5F6-4A5B-8C9D-0E1F2A3B4C5D";
        public const string ControlUUID = "A1B2C3D6-E5F6-4A5B-8C9D-0E1F2A3B4C5D";
        public const string DeviceName = "Inductosense Temp";
        public const int TotalBlocks = 1800;
        public const int BlockSize = 7168;
        public const int AckInterval = 20;

        // Waveform header structure (must match C struct layout)
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct WaveformHeader
        {
            public uint BlockNumber;
            public uint TimestampMs;
            public uint SampleRateHz;
            public ushort SampleCount;
            public ushort Reserved1;
            public ushort TriggerSample;
            public uint PulseFreqHz;
            public uint Reserved2;
            public short TemperatureCx10;
            public byte GainDb;
            public byte Reserved3;
            public uint Crc32;
            public ushort Reserved4;
        }

        // Transfer statistics structure
        [StructLayout(LayoutKind.Sequential)]
        public struct TransferStats
        {
            public uint BlocksReceived;
            public uint TotalBlocks;
            public uint TotalBytesReceived;
            public uint TotalChunksReceived;
            public double ThroughputKBps;
            public double ProgressPercent;
            public double ElapsedSeconds;
        }

        // Callback delegates
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void WaveformCallback(IntPtr waveform, [MarshalAs(UnmanagedType.I1)] bool isCompressed, IntPtr userData);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void ProgressCallback(IntPtr stats, IntPtr userData);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void CompletionCallback(IntPtr stats, IntPtr userData);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void AckCallback(ushort blockNumber, IntPtr userData);

        // Library initialization
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool psoc_driver_init();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void psoc_driver_cleanup();

        // Transfer session management
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr transfer_session_create();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void transfer_session_destroy(IntPtr session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void transfer_session_set_waveform_callback(IntPtr session, WaveformCallback callback, IntPtr userData);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void transfer_session_set_progress_callback(IntPtr session, ProgressCallback callback, IntPtr userData);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void transfer_session_set_completion_callback(IntPtr session, CompletionCallback callback, IntPtr userData);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void transfer_session_set_ack_callback(IntPtr session, AckCallback callback, IntPtr userData);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void transfer_session_start(IntPtr session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void transfer_session_stop(IntPtr session);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool transfer_session_process_chunk(IntPtr session, byte[] data, UIntPtr length);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void transfer_session_get_stats(IntPtr session, out TransferStats stats);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool transfer_session_is_active(IntPtr session);
    }
}
