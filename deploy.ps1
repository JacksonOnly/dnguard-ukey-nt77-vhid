$ErrorActionPreference = 'Stop'
$deviceId = 'Root\NT77VHID'
$subjectNeedle = 'DNGuard UKEY by XHM'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

function Write-Info([string]$Message) { Write-Host "[nt77] $Message" }
function Write-Fail([string]$Message) { Write-Host "[nt77] ERROR: $Message" -ForegroundColor Red }

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Restart-Elevated {
    $arguments = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', $MyInvocation.MyCommand.Path,
        '-Configuration', $Configuration)
    Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $arguments | Out-Null
}

function Confirm-Action([string]$Message) {
    $answer = Read-Host "$Message [Y/N]"
    return $answer -match '^(?i)y(es)?$'
}

function Find-Package {
    $expected = Join-Path $scriptRoot "driver\nt77vhid"
    $inf = Join-Path $expected 'nt77vhid.inf'
    $sys = Join-Path $expected 'nt77vhid.sys'
    $cat = Join-Path $expected 'nt77vhid.cat'
    $cer = Join-Path $scriptRoot 'nt77vhid.cer'
    if ((Test-Path -LiteralPath $inf) -and (Test-Path -LiteralPath $sys) -and
        (Test-Path -LiteralPath $cat) -and (Test-Path -LiteralPath $cer)) {
        return [pscustomobject]@{ Inf = $inf; Sys = $sys; Cat = $cat; Cer = $cer }
    }

    throw "找不到（INF/SYS/CAT/CER）。"
}

function Find-Devcon {
    $local = Join-Path $scriptRoot 'devcon.exe'
    if (Test-Path -LiteralPath $local) { return $local }
    $roots = @()
    if ($env:WindowsSdkDir) { $roots += Join-Path $env:WindowsSdkDir 'Tools' }
    if (${env:ProgramFiles(x86)}) { $roots += Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Tools' }
    if ($env:ProgramFiles) { $roots += Join-Path $env:ProgramFiles 'Windows Kits\10\Tools' }
    foreach ($root in ($roots | Select-Object -Unique)) {
        if (Test-Path $root) {
            $found = Get-ChildItem -LiteralPath $root -Recurse -File -Filter 'devcon.exe' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } |
            Sort-Object FullName -Descending | Select-Object -First 1
            if ($found) { return $found.FullName }
        }
    }
    $command = Get-Command devcon.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw '找不到 WDK devcon.exe。'
}

function Invoke-Native {
    param([string]$FilePath, [string[]]$ArgumentList, [switch]$AllowFailure)
    Write-Info ((@($FilePath) + $ArgumentList) -join ' ')
    $output = @(& $FilePath @ArgumentList 2>&1)
    $code = $LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    if (($code -ne 0) -and -not $AllowFailure) {
        throw "外部命令失败（退出码 $code）：$FilePath"
    }
    return [pscustomobject]@{ ExitCode = $code; Output = $output }
}

function Get-Certificate([string]$Path) {
    $certificate = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($Path)
    if ($certificate.Subject -notmatch [regex]::Escape($subjectNeedle)) {
        throw "证书主题不匹配：$($certificate.Subject)；需要 $subjectNeedle。"
    }
    return $certificate
}

function Test-CertInStore([string]$StoreName, [string]$Thumbprint) {
    $store = New-Object System.Security.Cryptography.X509Certificates.X509Store($StoreName, 'LocalMachine')
    try {
        $store.Open('ReadOnly')
        return [bool]($store.Certificates | Where-Object Thumbprint -eq $Thumbprint)
    }
    finally { $store.Close() }
}

function Import-CertificateIfNeeded([System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate,
    [string]$Path, [string]$StoreName) {
    if (Test-CertInStore $StoreName $Certificate.Thumbprint) {
        Write-Info "$StoreName 已包含证书 $($Certificate.Thumbprint)。"
        return $true
    }
    if (-not (Confirm-Action "将 $Path 导入 LocalMachine\\$StoreName")) { return $false }
    $store = New-Object System.Security.Cryptography.X509Certificates.X509Store($StoreName, 'LocalMachine')
    try {
        $store.Open('ReadWrite')
        $store.Add($Certificate)
    }
    finally { $store.Close() }
    Write-Info "已导入 $StoreName。"
    return $true
}

function Get-TestSigning {
    $result = Invoke-Native 'bcdedit.exe' @('/enum', '{current}') -AllowFailure
    if ($result.ExitCode -ne 0) { throw '无法读取当前启动配置。' }
    return [bool](($result.Output -join "`n") -match '(?im)^\s*testsigning\s+Yes\s*$')
}

function Test-RootDevice {
    $result = Invoke-Native $script:Devcon @('status', $deviceId) -AllowFailure
    $text = ($result.Output | Out-String)

    if ($result.ExitCode -ne 0) { return $false }
    if ($text -match '(?i)No matching devices found') { return $false }
    if ($text -match '(?i)\d+\s+matching device\(s\) found') { return $true }

    return $false
}
function Get-DriverMetadata {
    $dev = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
    Where-Object { $_.PNPDeviceID -like 'ROOT\SYSTEM\*' -and $_.Name -like '*NT77*' } |
    Select-Object -First 1
    if (-not $dev) { return $null }
    $drv = Get-CimInstance Win32_PnPSignedDriver -ErrorAction SilentlyContinue |
    Where-Object { $_.DeviceID -eq $dev.PNPDeviceID } |
    Select-Object -First 1
    return $drv | Select-Object DeviceID, DriverVersion, InfName, DriverProviderName
}

function Show-Status {
    $test = if (Get-TestSigning) { '开启' } else { '关闭' }
    $cert = Get-Certificate $script:Package.Cer
    $root = Test-RootDevice
    Write-Host "测试签名：$test"
    Write-Host "证书：$($cert.Subject) / Thumbprint=$($cert.Thumbprint)"
    Write-Host "Root 设备：$(if ($root) { '存在' } else { '不存在' })"
    $vhf = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Present -and (($_.PNPDeviceID -like 'HID\HID_DEVICE_SYSTEM_VHF*') -or
            ($_.HardwareID -contains 'HID\VID_0925&PID_A508'))
    }
    if ($vhf) { $vhf | Format-Table Status, Name, PNPDeviceID -AutoSize }
    else { Write-Host 'VHF HID 子设备：未找到' }
    $metadata = Get-DriverMetadata
    if ($metadata) {
        Write-Host "驱动版本：$($metadata.DriverVersion)"
        Write-Host "INF 名称：$($metadata.InfName)"
    }
    else { Write-Host '驱动版本：未找到签名驱动信息' }
}

function Install-Driver {
    if (Test-RootDevice) {
        Write-Info 'Root 设备已存在，安装请求转为更新。'
        Update-Driver
        return
    }

    if (-not (Confirm-Action "安装 $($script:Package.Inf) 到 $deviceId")) { return }

    $result = Invoke-Native $script:Devcon @('install', $script:Package.Inf, $deviceId) -AllowFailure
    if (-not (Test-RootDevice)) {
        throw "devcon install 失败（退出码 $($result.ExitCode)）且 Root 设备不存在。"
    }

    Write-Info '安装完成，执行一次更新以确保驱动绑定生效。'
    Update-Driver
}

function Update-Driver {
    if (-not (Test-RootDevice)) { throw 'Root 设备不存在；更新操作不会隐式安装。' }
    if (-not (Confirm-Action "使用 $($script:Package.Inf) 更新 $deviceId")) { return }
    Invoke-Native $script:Devcon @('update', $script:Package.Inf, $deviceId) -AllowFailure
    if (-not (Test-RootDevice)) { throw '更新后 Root 设备状态异常。' }
    $metadata = Get-DriverMetadata
    if (-not $metadata -or [string]::IsNullOrWhiteSpace($metadata.DriverVersion) -or
        [string]::IsNullOrWhiteSpace($metadata.InfName)) {
        throw '更新后无法核对驱动版本或 INF 名称。'
    }
    Write-Info "更新后的驱动版本=$($metadata.DriverVersion)，INF=$($metadata.InfName)。"
}

function Get-InstalledInfName {
    $metadata = Get-DriverMetadata
    if ($metadata -and $metadata.InfName -match '^oem\d+\.inf$') { return $metadata.InfName }
    return $null
}

function Uninstall-Driver {
    $infName = Get-InstalledInfName
    if ($infName) { Write-Info "将移除的 Driver Store 包：$infName" }
    if (-not (Confirm-Action "移除 Root 设备 $deviceId")) { return }
    Invoke-Native $script:Devcon @('remove', $deviceId) -AllowFailure
    if (Test-RootDevice) { throw 'devcon remove 返回成功但 Root 设备仍存在。' }
    if ($infName -and (Confirm-Action "从 Driver Store 删除 $infName")) {
        Invoke-Native 'pnputil.exe' @('/delete-driver', $infName, '/uninstall', '/force')
    }
    Write-Info '卸载完成。'
}

try {
    if (-not (Test-Administrator)) {
        Write-Info '需要管理员权限，正在请求 UAC 提升。'
        Restart-Elevated
        exit 0
    }

    $script:Package = Find-Package
    $script:Devcon = Find-Devcon
    Write-Info "配置=$Configuration"
    Write-Info "INF=$($Package.Inf)"
    Write-Info "SYS=$($Package.Sys)"
    Write-Info "CAT=$($Package.Cat)"
    Write-Info "CER=$($Package.Cer)"
    $certificate = Get-Certificate $Package.Cer
    Write-Info "证书主题已验证：$($certificate.Subject)"
    if (-not (Import-CertificateIfNeeded $certificate $Package.Cer 'Root')) { exit 0 }
    if (-not (Import-CertificateIfNeeded $certificate $Package.Cer 'TrustedPublisher')) { exit 0 }

    if (-not (Get-TestSigning)) {
        Write-Host '当前未开启测试签名。'
        if (Confirm-Action '执行 bcdedit /set testsigning on（成功后请自行重启）') {
            Invoke-Native 'bcdedit.exe' @('/set', 'testsigning', 'on')
            Write-Host '测试签名已设置为开启；请自行重启后再次运行本工具。'
            Pause
        }
        exit 0
    }

    while ($true) {
        Write-Host "`nNT77 部署工具 [$Configuration]"
        Write-Host '1. 安装  2. 更新  3. 卸载  4. 状态  5. 退出'
        $choice = Read-Host '请选择'
        try {
            switch ($choice) {
                '1' { Install-Driver }
                '2' { Update-Driver }
                '3' { Uninstall-Driver }
                '4' { Show-Status }
                '5' { exit 0 }
                default { Write-Host '请输入 1 到 5。' }
            }
        }
        catch { Write-Fail $_.Exception.Message }
    }
}
catch {
    Write-Fail $_.Exception.Message
    exit 1
}
