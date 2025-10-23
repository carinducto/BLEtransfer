using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Devices.Enumeration;
using Windows.Storage.Streams;
using BLETester.Interop;

namespace BLETester.Controllers
{
    /// <summary>
    /// BLE Controller - Layer 2: BLE Interface Control
    /// Handles Windows Bluetooth operations and delegates protocol logic to PSoC Driver (Layer 3)
    /// </summary>
    public class BLEController : INotifyPropertyChanged
    {
        // Protocol constants
        private static readonly Guid ServiceUuid = Guid.Parse(NativeMethods.ServiceUUID);
        private static readonly Guid DataBlockUuid = Guid.Parse(NativeMethods.DataBlockUUID);
        private static readonly Guid ControlUuid = Guid.Parse(NativeMethods.ControlUUID);

        // BLE objects
        private BluetoothLEDevice _device;
        private GattCharacteristic _dataBlockCharacteristic;
        private GattCharacteristic _controlCharacteristic;

        // Transfer session
        private TransferSession _transferSession;

        // Properties for UI binding
        private bool _isConnected;
        public bool IsConnected
        {
            get => _isConnected;
            set { _isConnected = value; OnPropertyChanged(); }
        }

        private string _connectionState = "Not connected";
        public string ConnectionState
        {
            get => _connectionState;
            set { _connectionState = value; OnPropertyChanged(); }
        }

        private string _deviceName = "";
        public string DeviceName
        {
            get => _deviceName;
            set { _deviceName = value; OnPropertyChanged(); }
        }

        private int _negotiatedMTU = 23;
        public int NegotiatedMTU
        {
            get => _negotiatedMTU;
            set { _negotiatedMTU = value; OnPropertyChanged(); }
        }

        private bool _isTransferActive;
        public bool IsTransferActive
        {
            get => _isTransferActive;
            set { _isTransferActive = value; OnPropertyChanged(); }
        }

        private Waveform _currentWaveform;
        public Waveform CurrentWaveform
        {
            get => _currentWaveform;
            set { _currentWaveform = value; OnPropertyChanged(); }
        }

        private string _currentMode = "Waiting...";
        public string CurrentMode
        {
            get => _currentMode;
            set { _currentMode = value; OnPropertyChanged(); }
        }

        private TransferStats _transferStats;
        public TransferStats TransferStats
        {
            get => _transferStats;
            set { _transferStats = value; OnPropertyChanged(); }
        }

        public event PropertyChangedEventHandler PropertyChanged;

        public BLEController()
        {
            InitializeTransferSession();
        }

        private void InitializeTransferSession()
        {
            _transferSession = new TransferSession();

            _transferSession.OnWaveform += (waveform) =>
            {
                CurrentWaveform = waveform;
                CurrentMode = waveform.IsCompressed ? "Compressed" : "Uncompressed";
            };

            _transferSession.OnProgress += (stats) =>
            {
                TransferStats = stats;
            };

            _transferSession.OnCompletion += (stats) =>
            {
                IsTransferActive = false;
                TransferStats = stats;
                Debug.WriteLine("Transfer complete!");
                Debug.WriteLine($"Blocks: {stats.BlocksReceived}/{stats.TotalBlocks}");
                Debug.WriteLine($"Data: {stats.TotalBytesReceived / 1024.0 / 1024.0:F2} MB");
                Debug.WriteLine($"Rate: {stats.ThroughputKBps:F2} KB/s");
            };

            _transferSession.OnAck += (blockNumber) =>
            {
                SendAck(blockNumber);
            };
        }

        public async Task ScanForDeviceAsync()
        {
            ConnectionState = "Scanning...";

            var selector = BluetoothLEDevice.GetDeviceSelectorFromDeviceName(NativeMethods.DeviceName);
            var devices = await DeviceInformation.FindAllAsync(selector);

            if (devices.Count > 0)
            {
                await ConnectToDeviceAsync(devices[0].Id);
            }
            else
            {
                ConnectionState = "Device not found";
            }
        }

        private async Task ConnectToDeviceAsync(string deviceId)
        {
            try
            {
                ConnectionState = "Connecting...";
                _device = await BluetoothLEDevice.FromIdAsync(deviceId);

                if (_device == null)
                {
                    ConnectionState = "Connection failed";
                    return;
                }

                DeviceName = _device.Name;
                IsConnected = true;
                ConnectionState = "Connected";

                // Discover services
                var servicesResult = await _device.GetGattServicesForUuidAsync(ServiceUuid);
                if (servicesResult.Status == GattCommunicationStatus.Success && servicesResult.Services.Count > 0)
                {
                    var service = servicesResult.Services[0];

                    // Get characteristics
                    var dataCharResult = await service.GetCharacteristicsForUuidAsync(DataBlockUuid);
                    var controlCharResult = await service.GetCharacteristicsForUuidAsync(ControlUuid);

                    if (dataCharResult.Status == GattCommunicationStatus.Success &&
                        controlCharResult.Status == GattCommunicationStatus.Success)
                    {
                        _dataBlockCharacteristic = dataCharResult.Characteristics[0];
                        _controlCharacteristic = controlCharResult.Characteristics[0];

                        // Subscribe to notifications
                        await _dataBlockCharacteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                            GattClientCharacteristicConfigurationDescriptorValue.Notify);

                        _dataBlockCharacteristic.ValueChanged += OnDataBlockValueChanged;

                        Debug.WriteLine("Ready to start transfer");
                    }
                }
            }
            catch (Exception ex)
            {
                ConnectionState = $"Error: {ex.Message}";
                Debug.WriteLine($"Connection error: {ex.Message}");
            }
        }

        private void OnDataBlockValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
        {
            var reader = DataReader.FromBuffer(args.CharacteristicValue);
            var data = new byte[reader.UnconsumedBufferLength];
            reader.ReadBytes(data);

            // Process chunk through driver
            _transferSession?.ProcessChunk(data);
        }

        public async Task StartTransferAsync()
        {
            if (_controlCharacteristic == null) return;

            var writer = new DataWriter();
            writer.WriteByte(0x01); // CMD_START
            writer.WriteUInt16(0);
            writer.WriteUInt32((uint)DateTimeOffset.UtcNow.ToUnixTimeSeconds());

            await _controlCharacteristic.WriteValueAsync(writer.DetachBuffer(), GattWriteOption.WriteWithoutResponse);

            _transferSession?.Start();
            IsTransferActive = true;

            Debug.WriteLine("Transfer started");
        }

        public async Task StopTransferAsync()
        {
            if (_controlCharacteristic == null) return;

            var writer = new DataWriter();
            writer.WriteByte(0x02); // CMD_STOP
            writer.WriteUInt16(0);
            writer.WriteUInt32((uint)DateTimeOffset.UtcNow.ToUnixTimeSeconds());

            await _controlCharacteristic.WriteValueAsync(writer.DetachBuffer(), GattWriteOption.WriteWithoutResponse);

            _transferSession?.Stop();
            IsTransferActive = false;

            Debug.WriteLine("Transfer stopped");
        }

        private async void SendAck(ushort blockNumber)
        {
            if (_controlCharacteristic == null) return;

            var writer = new DataWriter();
            writer.WriteByte(0x03); // CMD_ACK
            writer.WriteUInt16(blockNumber);
            writer.WriteUInt32((uint)DateTimeOffset.UtcNow.ToUnixTimeSeconds());

            await _controlCharacteristic.WriteValueAsync(writer.DetachBuffer(), GattWriteOption.WriteWithoutResponse);
        }

        protected virtual void OnPropertyChanged([CallerMemberName] string propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}
