[CmdletBinding()]
param(
    [string]$BuildRoot
)

$ErrorActionPreference = 'Stop'
if (-not $PSBoundParameters.ContainsKey('BuildRoot')) { $BuildRoot = Join-Path $PSScriptRoot 'work\nt77-tests' }
$script:Root = Split-Path -Parent $PSCommandPath
$root = Split-Path -Parent $PSCommandPath

function Assert-SigningSubject([string]$Path, [string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "签名检查失败：找不到 $Path" }
    $signature = Get-AuthenticodeSignature -FilePath $Path
    $signer = $signature.SignerCertificate.Subject
    if ([string]::IsNullOrWhiteSpace($signer) -or
        $signer -notlike "*$Expected*") {
        throw "签名主题不匹配：$Path`n实际：$signer`n需要：$Expected"
    }
    Write-Host "签名通过：$Path"
    Write-Host "签名者：$signer"
}

function Assert-CertificateSubject([string]$Path, [string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "证书检查失败：找不到 $Path" }
    $certificate = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($Path)
    if ($certificate.Subject -notlike "*$Expected*") {
        throw "证书主题不匹配：$Path`n实际：$($certificate.Subject)`n需要：$Expected"
    }
    Write-Host "证书通过：$Path"
    Write-Host "主题：$($certificate.Subject)"
}

& cmake -S $root -B $BuildRoot
if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败（退出码 $LASTEXITCODE）" }

& cmake --build $BuildRoot --config Debug
if ($LASTEXITCODE -ne 0) { throw "CMake 构建失败（退出码 $LASTEXITCODE）" }

& ctest --test-dir $BuildRoot -C Debug --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "CTest 失败（退出码 $LASTEXITCODE）" }

$certificateSubject = 'DNGuard UKEY by XHM'
$fixedRoot = Join-Path $root 'driver\x64\Fixed'
Assert-CertificateSubject (Join-Path $fixedRoot 'nt77vhid.cer') $certificateSubject
Assert-SigningSubject (Join-Path $fixedRoot 'nt77vhid.sys') $certificateSubject
Assert-SigningSubject (Join-Path $fixedRoot 'nt77vhid\nt77vhid.cat') $certificateSubject

Write-Host '协议测试与 Fixed 驱动签名检查全部通过。'

