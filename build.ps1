[CmdletBinding()]
param(
    [ValidateSet('All', 'Debug', 'Fixed')]
    [string]$Configuration = 'All'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

function Find-MsBuild {
    $known = 'D:\Softwares\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
    if (Test-Path -LiteralPath $known) { return $known }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw '找不到 vswhere.exe。请确认已安装 Visual Studio 2022。'
    }
    $installation = & $vswhere -latest -products * `
        -requires Microsoft.Component.MSBuild `
        -property installationPath | Select-Object -First 1
    if (-not $installation) { throw '找不到 Visual Studio 2022。' }
    $msbuild = Join-Path $installation 'MSBuild\Current\Bin\amd64\MSBuild.exe'
    if (-not (Test-Path -LiteralPath $msbuild)) { throw '找不到 MSBuild.exe。' }
    return $msbuild
}

function Build-Driver([string]$Name) {
    Write-Host "构建驱动 $Name|x64..."
    $arguments = @(
        (Join-Path $root 'driver\nt77vhid.vcxproj'),
        '/t:Rebuild',
        "/p:Configuration=$Name",
        '/p:Platform=x64',
        '/m'
    )
    if ($Name -eq 'Fixed') {
        $arguments += "/p:TestCertificate=$script:CertificateThumbprint"
    }
    & $script:MsBuild @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "驱动构建失败：$Name|x64（退出码 $LASTEXITCODE）"
    }
}

$script:MsBuild = Find-MsBuild
$script:CertificateThumbprint = & (Join-Path $root 'new-test-certificate.ps1')
Write-Host "测试证书：$script:CertificateThumbprint"
if ($Configuration -in @('All', 'Debug')) { Build-Driver 'Debug' }
if ($Configuration -in @('All', 'Fixed')) { Build-Driver 'Fixed' }
Write-Host '驱动构建完成。'

