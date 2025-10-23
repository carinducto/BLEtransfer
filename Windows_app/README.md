# Windows BLE Tester - WPF Application

Windows desktop application for testing PSoC6 BLE devices using the shared C++ driver library.

## Architecture

This application follows the same three-layer architecture as the macOS version:

- **Layer 1**: WPF UI (MainWindow.xaml, WaveformView.xaml)
- **Layer 2**: Windows BLE Interface (BLEController.cs using Windows.Devices.Bluetooth)
- **Layer 3**: Shared C++ Driver Library (from `../shared_driver`)

## Prerequisites

- Windows 10/11
- Visual Studio 2022 or later
- .NET 8.0 SDK
- Windows SDK (for Bluetooth LE support)
- CMake 3.15+ (to build C++ driver library)

## Building

### 1. Build the C++ Driver Library

```powershell
cd ..\shared_driver
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

This creates `psoc_driver.lib` in `shared_driver\build\Release\`.

### 2. Build the Windows App

Open `BLETester.sln` in Visual Studio and build, or use command line:

```powershell
dotnet build BLETester.csproj -c Release
```

## Project Structure

```
Windows_app/
├── BLETester.sln           # Visual Studio solution
├── BLETester.csproj        # .NET project file
├── App.xaml                # Application entry point
├── App.xaml.cs
├── MainWindow.xaml         # Main UI window
├── MainWindow.xaml.cs
├── Views/
│   └── WaveformView.xaml   # Waveform display control
├── Controllers/
│   └── BLEController.cs    # BLE interface (Layer 2)
├── Interop/
│   ├── PSoCDriver.cs       # C# wrapper for C++ library
│   └── NativeMethods.cs    # P/Invoke declarations
└── README.md
```

## Features

- Device scanning and connection
- Real-time waveform visualization
- Transfer progress tracking
- Support for both compressed and uncompressed data
- Statistics display (throughput, blocks received, etc.)

## Implementation Notes

### Calling the C++ Library from C#

The `Interop/NativeMethods.cs` file uses P/Invoke to call the C++ driver functions:

```csharp
[DllImport("psoc_driver.dll", CallingConvention = CallingConvention.Cdecl)]
public static extern IntPtr transfer_session_create();

[DllImport("psoc_driver.dll", CallingConvention = CallingConvention.Cdecl)]
public static extern void transfer_session_start(IntPtr session);
```

### Windows BLE API

The `BLEController.cs` uses the Windows Runtime Bluetooth APIs:

```csharp
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
```

These APIs are part of Windows 10/11 and provide cross-platform BLE support.

## License

See LICENSE file in the repository root.
