[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [string]$PackageName = 'nt77vhid-fixed'
)

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$ErrorActionPreference = 'Stop'
if (-not $PSBoundParameters.ContainsKey('OutputDirectory')) { $OutputDirectory = Join-Path $PSScriptRoot 'publish' }
$scriptRoot = $PSScriptRoot
$configuration = 'Fixed'
$packageName = 'DNGuard UKEY by XHM'
$deviceId = 'Root\NT77VHID'

function Write-Info([string]$Message) { Write-Host "[publish] $Message" }

function Get-RequiredFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "缺少发布文件：$Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Get-DriverVersion([string]$InfPath) {
    $driverVerLine = (Select-String -LiteralPath $InfPath -Pattern '^\s*DriverVer\s*=' |
        Select-Object -First 1).Line
    if (-not $driverVerLine) { return 'unknown' }

    $value = $driverVerLine.Split('=', 2)[1].Trim()
    $parts = $value.Split(',', 2)
    $date = [datetime]::ParseExact($parts[0].Trim(), 'MM/dd/yyyy', [cultureinfo]::InvariantCulture)
    $time = $parts[1].Trim().Split('.')
    if ($time.Count -lt 4) { return 'unknown' }
    return '{0}{1}{2}-{3}{4}{5}' -f @(
        $date.Year, $date.Month.ToString('00'), $date.Day.ToString('00'),
        $time[0].PadLeft(2, '0'), $time[1], $time[2]
    )
}

function Add-TextFile([System.IO.Compression.ZipArchive]$Archive, [string]$EntryName, [string]$Content) {
    $entry = $Archive.CreateEntry($EntryName, [System.IO.Compression.CompressionLevel]::Optimal)
    $writer = New-Object System.IO.StreamWriter($entry.Open(), (New-Object System.Text.UTF8Encoding($false)))
    try {
        $writer.Write($Content)
    } finally {
        $writer.Dispose()
    }
}

$packageDirectory = Join-Path $scriptRoot "driver\x64\$configuration\nt77vhid"
$inf = Get-RequiredFile (Join-Path $packageDirectory 'nt77vhid.inf')
$sys = Get-RequiredFile (Join-Path $packageDirectory 'nt77vhid.sys')
$cat = Get-RequiredFile (Join-Path $packageDirectory 'nt77vhid.cat')
$cer = Get-RequiredFile (Join-Path $scriptRoot "driver\x64\$configuration\nt77vhid.cer")
$devcon = Get-RequiredFile (Join-Path $scriptRoot 'devcon.exe')
$deployCmd = Get-RequiredFile (Join-Path $scriptRoot 'deploy.cmd')
$deployPs1 = Get-RequiredFile (Join-Path $scriptRoot 'deploy.ps1')

$version = Get-DriverVersion $inf
$stagingRoot = Join-Path $OutputDirectory "$PackageName-$version"
$packageSubdirectory = 'driver\nt77vhid'
$stageDirectory = Join-Path $stagingRoot $packageSubdirectory

if (Test-Path -LiteralPath $stagingRoot) {
    throw "发布目录已存在：$stagingRoot"
}

try {
    New-Item -ItemType Directory -Path $stageDirectory -Force | Out-Null
    Copy-Item -LiteralPath $inf -Destination (Join-Path $stageDirectory 'nt77vhid.inf')
    Copy-Item -LiteralPath $sys -Destination (Join-Path $stageDirectory 'nt77vhid.sys')
    Copy-Item -LiteralPath $cat -Destination (Join-Path $stageDirectory 'nt77vhid.cat')
    Copy-Item -LiteralPath $cer -Destination (Join-Path $stagingRoot 'nt77vhid.cer')
    Copy-Item -LiteralPath $devcon -Destination (Join-Path $stagingRoot 'devcon.exe')
    Copy-Item -LiteralPath $deployCmd -Destination (Join-Path $stagingRoot 'deploy.cmd')
    Copy-Item -LiteralPath $deployPs1 -Destination (Join-Path $stagingRoot 'deploy.ps1')

    $readme = @"
NT77 Virtual HID 固定配置部署包
=============================

本包只包含 Fixed 配置。Fixed 是固定 serial、登录摘要、硬件 ID 和 NTRead 存储数据的
正式发布形态；在本项目中它等同于 Release。包内驱动不接受 serial、login、hardware-id、
write、write-file、clear 等任何外部配置修改，只保留状态查询和协议功能。

包内文件
--------
README.txt                 本说明
deploy.cmd                 Windows 部署管理入口
deploy.ps1                 部署管理脚本，由 deploy.cmd 调用
devcon.exe                 WDK 设备管理工具
nt77vhid.cer               测试签名证书，用于信任发布者
driver\nt77vhid\nt77vhid.inf    驱动安装信息文件
driver\nt77vhid\nt77vhid.sys    驱动程序
driver\nt77vhid\nt77vhid.cat    驱动程序目录签名

系统要求与风险提示
------------------
1. 仅用于 64 位 Windows 10/11 测试环境。
2. 必须启用 Windows 测试签名。启用会改变系统启动策略，通常需要重启。
3. 如启用了 BitLocker，先准备恢复密钥。
4. 如启用了 Secure Boot，可能需要先在固件设置中关闭。
5. 这是测试签名驱动，不要在无法接受风险的生产系统上使用。

首次部署
--------
1. 解压本 ZIP 到本地任意英文或中文路径。
2. 在 deploy.cmd 所在目录右键打开管理员 PowerShell 或管理员命令提示符。
3. 运行 deploy.cmd。
4. 首次运行会请求管理员权限，导入 nt77vhid.cer 到本机 Root 和 TrustedPublisher。
5. 如果测试签名未开启，脚本会询问是否执行：
   bcdedit /set testsigning on
   输入 Y 确认后，自行重启 Windows。
6. 重启后再运行 deploy.cmd，选择 1 安装。
7. 安装完成后选择 4 查看状态，确认 Root 设备和 VHF HID 子设备存在。

更新驱动
--------
1. 先用新包替换本目录，或解压新 ZIP。
2. 以管理员身份运行 deploy.cmd。
3. 脚本导入新证书后选择 2 更新。
4. 更新完成后选择 4 查看驱动版本、INF 名称和 VHF HID 子设备状态。

卸载
----
1. 以管理员身份运行 deploy.cmd。
2. 选择 3 卸载。
3. 确认后脚本会移除 $deviceId。
4. 脚本会显示 Driver Store 中对应的 oemNN.inf，并询问是否删除。
   输入 Y 才会执行 pnputil /delete-driver。
5. 如果不再需要测试签名，可自行执行：
   bcdedit /set testsigning off
   然后重启 Windows。

重要说明
--------
1. deploy.cmd 只能从本包根目录运行；它会根据自身位置定位驱动和证书。
2. devcon.exe 必须保留在包根目录。
3. 不要修改包内文件名或相对路径。
4. 部署脚本会验证证书主题，必须是：$packageName。
5. 导入根证书、开启测试签名、安装、更新、卸载都会先显示目标并要求确认。
6. 脚本不会自动关闭 Secure Boot，不会绕过 UAC，也不会自动重启。
"@
    $zipPath = Join-Path $OutputDirectory "$PackageName-$version.zip"
    $archive = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in Get-ChildItem -LiteralPath $stagingRoot -Recurse -File) {
            $relativePath = $file.FullName.Substring($stagingRoot.Length).TrimStart('\')
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $relativePath,
                [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
        Add-TextFile $archive 'README.txt' $readme
    } finally {
        $archive.Dispose()
    }

    Write-Info "已创建发布包：$zipPath"
    Write-Info "驱动版本：$version"
} catch {
    if ($zipPath -and (Test-Path -LiteralPath $zipPath)) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    throw
} finally {
    if (Test-Path -LiteralPath $stagingRoot) {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
}
