param(
    [Parameter(Mandatory=$true)][string]$SignTool,
    [Parameter(Mandatory=$true)][string]$CertificateThumbprint,
    [string]$DriverPath
)
$ErrorActionPreference = 'Stop'
if (!$DriverPath) { $DriverPath = Join-Path (Split-Path $PSScriptRoot -Parent) 'build\driver\Release\DarkEdenReader.sys' }
$binary = (Resolve-Path -LiteralPath $DriverPath).Path
$tool = (Resolve-Path -LiteralPath $SignTool).Path
$thumbprint = $CertificateThumbprint.Replace(' ', '')
if ($thumbprint -notmatch '^[0-9A-Fa-f]{40}$') { throw 'Invalid certificate thumbprint.' }
$certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$thumbprint"
if (!$certificate.HasPrivateKey) { throw 'The signing certificate has no private key.' }
if ($certificate.NotAfter -le (Get-Date)) { throw 'The signing certificate has expired.' }
& $tool sign /v /fd SHA256 /ph /s My /sha1 $thumbprint $binary
if ($LASTEXITCODE -ne 0) { throw 'Signing failed.' }
$signature = Get-AuthenticodeSignature -LiteralPath $binary
if (!$signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ne $thumbprint) {
    throw 'The expected signer was not embedded.'
}
$publicCertificate = Join-Path (Split-Path $binary -Parent) 'DarkEdenReader-Signer.cer'
Export-Certificate -Cert $certificate -FilePath $publicCertificate -Force | Out-Null
Write-Output "Signed: $binary"
Write-Output "Windows trust status: $($signature.Status) - $($signature.StatusMessage)"
Write-Output 'Signing does not add the certificate to Trusted Root or change boot policy.'
