# Build Instructions

## Prerequisites

### All Platforms
- CMake 3.15 or higher
- C++11 compatible compiler
- zlib library

### macOS
- macOS 13.0 or higher
- Xcode Command Line Tools (`xcode-select --install`)

### Windows
- Windows 10/11
- Visual Studio 2022 or Visual Studio Build Tools
- .NET 8.0 SDK
- Windows SDK

## Step-by-Step Build Guide

### 1. Build the Shared C++ Driver Library

This step is **required** for both macOS and Windows applications.

#### On macOS:

```bash
cd shared_driver
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

Output: `shared_driver/build/libpsoc_driver.a`

#### On Windows (PowerShell):

```powershell
cd shared_driver
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

Output: `shared_driver\build\Release\psoc_driver.lib`

### 2. Build the macOS Application

**Platform:** macOS only

```bash
cd macOS_app
chmod +x build.sh  # First time only
./build.sh
```

The build script will:
1. Check that the driver library exists
2. Create a bridging header for Swift/C++ interop
3. Compile all Swift sources
4. Link with the C++ driver library and system frameworks

Output: `macOS_app/build/BLETester` (arm64 executable)

**Running:**
```bash
./build/BLETester
```

### 3. Build the Windows Application

**Platform:** Windows only

```powershell
cd Windows_app
dotnet build -c Release
```

**Running:**
```powershell
dotnet run
```

Or double-click the executable in `bin\Release\net8.0-windows10.0.19041.0\`

## Verification

### Verify C++ Library Build

**macOS:**
```bash
ls -lh shared_driver/build/libpsoc_driver.a
file shared_driver/build/libpsoc_driver.a
```

Expected output:
```
-rw-r--r--  1 user  staff   XXX KB ... libpsoc_driver.a
libpsoc_driver.a: current ar archive random library
```

**Windows:**
```powershell
dir shared_driver\build\Release\psoc_driver.lib
```

### Verify macOS App Build

```bash
ls -lh macOS_app/build/BLETester
file macOS_app/build/BLETester
```

Expected output:
```
-rwxr-xr-x  1 user  staff   582K ... BLETester
BLETester: Mach-O 64-bit executable arm64
```

### Verify Windows App Build

```powershell
dir Windows_app\bin\Release\net8.0-windows10.0.19041.0\BLETester.exe
```

## Troubleshooting

### macOS

**Issue: "cannot find -lpsoc_driver"**
- Solution: Build the shared driver library first (step 1)

**Issue: "zlib not found"**
- Solution: Install via Homebrew: `brew install zlib`

**Issue: "Command Line Tools not found"**
- Solution: Install with `xcode-select --install`

**Issue: Swift compiler errors**
- Solution: Delete build artifacts: `rm -rf macOS_app/.build macOS_app/build`
- Then rebuild: `cd macOS_app && ./build.sh`

### Windows

**Issue: "CMake not found"**
- Solution: Install CMake and add to PATH

**Issue: "Cannot find compiler"**
- Solution: Install Visual Studio 2022 Build Tools with C++ support

**Issue: ".NET SDK not found"**
- Solution: Download from https://dotnet.microsoft.com/download

**Issue: "psoc_driver.dll not found at runtime"**
- Solution: Ensure driver library is built and copied to output directory
- Check .csproj file has correct paths

### Both Platforms

**Issue: "zlib errors during driver build"**
- Solution: Install zlib development files
  - macOS: `brew install zlib`
  - Windows: zlib is typically included with Visual Studio

**Issue: "Linker errors with C++ standard library"**
- macOS: Ensure `-lc++` is in build script (already included)
- Windows: Check project is set to use C++ runtime

## Clean Build

To perform a complete clean build:

### macOS:
```bash
# Clean driver
cd shared_driver && rm -rf build && cd ..

# Clean app
cd macOS_app && rm -rf build .build && cd ..

# Rebuild
cd shared_driver && mkdir build && cd build && cmake .. && make && cd ../..
cd macOS_app && ./build.sh
```

### Windows:
```powershell
# Clean driver
cd shared_driver
rm -r -fo build
cd ..

# Clean app
cd Windows_app
dotnet clean
cd ..

# Rebuild
cd shared_driver
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
cd ..\..
cd Windows_app
dotnet build -c Release
```

## Development Setup

### macOS with Xcode (Optional)

While the build script works well, you can also use Xcode for development:

1. Open macOS_app folder in Xcode
2. Add the shared driver library to Link Binary with Libraries
3. Configure header search paths to include `../shared_driver/include`
4. Build and run directly from Xcode

### Windows with Visual Studio

1. Open `Windows_app/BLETester.sln` in Visual Studio 2022
2. Ensure shared driver library is built
3. Build and run from Visual Studio

## Running Tests

Currently, the applications are the primary test mechanism. To test:

1. Ensure PSoC6 device is powered on and advertising
2. Run the macOS or Windows app
3. Click "Scan for Device"
4. Once connected, click "Start Transfer"
5. Verify waveform visualization updates
6. Check statistics are updating correctly

## Next Steps

After successful build:
- See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed architecture documentation
- See platform-specific READMEs for usage instructions:
  - `shared_driver/README.md` - Driver library API reference
  - `macOS_app/Sources/BLETester/README.md` - macOS app details
  - `Windows_app/README.md` - Windows app details

## Build Script Customization

### macOS build.sh

To modify build flags, edit `macOS_app/build.sh`:
- Add `-O2` for optimizations
- Add `-g` for debug symbols
- Modify framework links as needed

### Windows .csproj

To modify Windows build, edit `Windows_app/BLETester.csproj`:
- Change target framework
- Add NuGet packages
- Modify build properties

## Getting Help

If you encounter build issues:
1. Check this troubleshooting section
2. Verify all prerequisites are installed
3. Try a clean build
4. Check that file paths match your system
5. Review error messages for missing dependencies
