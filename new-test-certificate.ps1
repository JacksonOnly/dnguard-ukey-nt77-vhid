[CmdletBinding()]
param(
    [string]$SubjectName = 'DNGuard UKEY by XHM',
    [int]$Years = 10
)

$ErrorActionPreference = 'Stop'
$store = New-Object System.Security.Cryptography.X509Certificates.X509Store(
    [System.Security.Cryptography.X509Certificates.StoreName]::My,
    [System.Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser)
$store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
$existing = $store.Certificates | Where-Object {
    $_.Subject -ieq "CN=$SubjectName" -or $_.FriendlyName -ieq $SubjectName
}
$store.Close()

foreach ($certificate in $existing) {
    if ($certificate.NotAfter -gt (Get-Date)) {
        return $certificate.Thumbprint
    }
}

$certificate = New-SelfSignedCertificate `
    -Subject "CN=$SubjectName" `
    -FriendlyName $SubjectName `
    -CertStoreLocation Cert:\CurrentUser\My `
    -KeyAlgorithm RSA -KeyLength 2048 `
    -HashAlgorithm SHA256 `
    -KeyExportPolicy Exportable `
    -KeyUsage DigitalSignature, KeyEncipherment `
    -Type CodeSigningCert `
    -NotAfter (Get-Date).AddYears($Years)
Write-Host "已创建测试证书：$($certificate.Thumbprint)" -ForegroundColor Green
return $certificate.Thumbprint

