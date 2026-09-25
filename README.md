# NT77加密狗模拟 (DNGuard 虚拟UKEY驱动)

[简体中文](#简体中文) | [English](#english)

## 简体中文

当前的源码编译后可以搭配 DNGuard-Enterprise-v4.922CN.rar 进行使用。  
环境需要为Win 10 /11 x64 测试模式下，可以为虚拟机，然后可查看Release中已经编译的驱动，进行安装，  
安装之后可直接打开DNGuard进行使用，不需要联网。

请仅将本项目用于你有权使用和测试的软件及设备环境。驱动不提供第三方软件许可，也不应被用于绕过软件授权或访问控制。

设备使用 VID `0925`、PID `A508`。Feature Report 为 49 字节：1 字节 Report ID 加 48 字节协议帧。项目还提供 `nt77ctl` 配置工具、可移植的 CMake 协议测试，以及驱动构建和部署脚本。

### 功能

- 模拟 serial、登录、硬件 ID、读写存储和会话握手等 NT77 协议操作。
- `Debug` 配置支持通过 `nt77ctl` 在运行时设置设备数据；配置保存在内存中，驱动重启后需要重新设置。
- `Fixed` 配置使用编译时固定的数据，并拒绝外部配置写入，适合生成固定配置的测试包。
- `nt77ctl` 提供状态查询、数据配置和存储写入命令。
- CMake 测试覆盖协议处理和工具参数行为。

### 构建要求

- Windows 10/11 x64
- Visual Studio 2022，安装 Desktop development with C++ 工作负载
- Windows Driver Kit (WDK) 10.0.26100.0
- CMake 3.20 或更高版本；Windows 上构建 `nt77ctl` 还需要 Visual C++ 工具链

### 构建驱动

在仓库根目录打开 PowerShell：

```powershell
# 只构建可运行时配置的 Debug 驱动
.\build.ps1 -Configuration Debug

# 构建固定配置驱动
.\build.ps1 -Configuration Fixed

# 同时构建 Debug 和 Fixed
.\build.ps1 -Configuration All
```

脚本会查找 Visual Studio/MSBuild，并准备本机测试签名证书。驱动包分别输出到 `driver\x64\Debug\nt77vhid` 或 `driver\x64\Fixed\nt77vhid`；构建生成的文件不需要提交到版本库。

### 构建协议测试和命令行工具

```powershell
cmake -S . -B out/build
cmake --build out/build --config Debug
ctest --test-dir out/build -C Debug --output-on-failure
```

Windows 构建会额外生成 `nt77ctl`。协议单元测试可在支持 CMake 的 Windows、Linux 或 macOS 环境构建；VHF 驱动本身仅适用于 Windows。

### 配置工具用法

在安装并启动驱动后，以管理员身份运行命令提示符或 PowerShell。以下示例假设 `nt77ctl.exe` 位于 `bin` 目录；若从 CMake 构建，请使用构建目录中实际生成的可执行文件路径。

```powershell
bin\nt77ctl.exe serial "要匹配的 ASCII 序列号"
bin\nt77ctl.exe login "登录用 ASCII 字符串"
bin\nt77ctl.exe login-md5 00112233445566778899AABBCCDDEEFF
bin\nt77ctl.exe hardware-id b438c8472f3a494ea95e585ca81ee468
bin\nt77ctl.exe write 0 01020304AABBCCDD
bin\nt77ctl.exe write-file 128 D:\data\sector.bin
bin\nt77ctl.exe status
bin\nt77ctl.exe clear-storage
```

`serial` 接受最多 32 字节的 ASCII 字符串。`login` 接受最多 128 字节的 ASCII 字符串，并按照协议约定对 256 字节零填充数据计算 MD5；也可以用 `login-md5` 直接提供 16 字节摘要。`hardware-id` 接受 16 或 24 字节十六进制数据；提供 16 字节时，末尾 8 字节补零。存储容量为 1024 字节，地址范围为 `0–1023`。`NTRead` 和 `NTWrite` 需要登录成功；硬件 ID 查询不要求登录，但需要已配置硬件 ID。每次更改配置都会重置协议会话。

完整命令列表可运行 `nt77ctl.exe` 查看。`nt77ctl --self-test` 可检查工具内置的解析与摘要计算逻辑。

### 打包与部署

构建 `Fixed` 配置后，可在仓库根目录运行：

```powershell
.\publish.ps1
```

发布包会生成在 `publish` 目录。解压后，以管理员身份运行包内的 `deploy.cmd`，按提示导入测试证书并安装、更新、查看或卸载设备。

该驱动使用测试签名。启用 Windows 测试签名会修改启动配置，通常需要重启；Secure Boot 可能阻止测试签名驱动加载。如果系统启用了 BitLocker，请先确认恢复密钥可用。只应在你控制的测试环境中部署，并在不再需要时按需关闭测试签名。

## English

# NT77 Dongle Emulator (DNGuard Virtual UKEY Driver)

The current source code, once compiled, can be used together with DNGuard-Enterprise-v4.922CN.rar.  
The environment must be Windows 10/11 x64 in Test Mode; a virtual machine is acceptable. You can then check the precompiled driver in Releases and install it.  
After installation, you can directly open DNGuard and use it without an internet connection.

Please use this project only for software and device environments that you are authorized to use and test. The driver does not provide third-party software licenses and should not be used to bypass software authorization or access control.

The device uses VID `0925` and PID `A508`. The Feature Report is 49 bytes: 1 byte Report ID plus a 48-byte protocol frame. The project also provides the `nt77ctl` configuration tool, portable CMake protocol tests, and driver build and deployment scripts.

### Features

- Emulates NT77 protocol operations such as serial, login, hardware ID, read/write storage, and session handshake.
- The `Debug` configuration supports setting device data at runtime via `nt77ctl`; the configuration is kept in memory and must be set again after the driver restarts.
- The `Fixed` configuration uses compile-time fixed data and rejects external configuration writes, making it suitable for generating test packages with fixed configurations.
- `nt77ctl` provides status query, data configuration, and storage write commands.
- CMake tests cover protocol handling and tool argument behavior.

### Build Requirements

- Windows 10/11 x64
- Visual Studio 2022 with the Desktop development with C++ workload installed
- Windows Driver Kit (WDK) 10.0.26100.0
- CMake 3.20 or later; building `nt77ctl` on Windows also requires the Visual C++ toolchain

### Building the Driver

Open PowerShell at the repository root:

```powershell
# Build only the runtime-configurable Debug driver
.\build.ps1 -Configuration Debug

# Build the fixed-configuration driver
.\build.ps1 -Configuration Fixed

# Build both Debug and Fixed
.\build.ps1 -Configuration All
```

The script locates Visual Studio/MSBuild and prepares a local test signing certificate. Driver packages are output to `driver\x64\Debug\nt77vhid` or `driver\x64\Fixed\nt77vhid` respectively; build-generated files do not need to be committed to the repository.

### Building Protocol Tests and the Command-Line Tool

```powershell
cmake -S . -B out/build
cmake --build out/build --config Debug
ctest --test-dir out/build -C Debug --output-on-failure
```

Windows builds additionally produce `nt77ctl`. Protocol unit tests can be built on Windows, Linux, or macOS environments that support CMake; the VHF driver itself is Windows-only.

### Configuration Tool Usage

After the driver is installed and started, run Command Prompt or PowerShell as Administrator. The following examples assume `nt77ctl.exe` is located in the `bin` directory; if building from CMake, use the actual path to the executable generated in the build directory.

```powershell
bin\nt77ctl.exe serial "ASCII serial number to match"
bin\nt77ctl.exe login "ASCII string for login"
bin\nt77ctl.exe login-md5 00112233445566778899AABBCCDDEEFF
bin\nt77ctl.exe hardware-id b438c8472f3a494ea95e585ca81ee468
bin\nt77ctl.exe write 0 01020304AABBCCDD
bin\nt77ctl.exe write-file 128 D:\data\sector.bin
bin\nt77ctl.exe status
bin\nt77ctl.exe clear-storage
```

`serial` accepts an ASCII string of up to 32 bytes. `login` accepts an ASCII string of up to 128 bytes and computes the MD5 over 256 bytes of zero-padded data according to the protocol convention; alternatively, `login-md5` can be used to provide the 16-byte digest directly. `hardware-id` accepts 16 or 24 bytes of hexadecimal data; when 16 bytes are provided, the trailing 8 bytes are zero-padded. Storage capacity is 1024 bytes with an address range of `0–1023`. `NTRead` and `NTWrite` require a successful login; the hardware ID query does not require login but requires a configured hardware ID. Every configuration change resets the protocol session.

Run `nt77ctl.exe` for the full command list. `nt77ctl --self-test` checks the tool's built-in parsing and digest computation logic.

### Packaging and Deployment

After building the `Fixed` configuration, run the following at the repository root:

```powershell
.\publish.ps1
```

The release package is generated in the `publish` directory. After extracting, run the included `deploy.cmd` as Administrator and follow the prompts to import the test certificate and install, update, view, or uninstall the device.

The driver uses test signing. Enabling Windows test signing modifies the boot configuration and usually requires a reboot; Secure Boot may prevent test-signed drivers from loading. If BitLocker is enabled on your system, confirm that the recovery key is available first. Deploy only in a test environment you control, and disable test signing as needed when it is no longer required.