#Requires -RunAsAdministrator
param([Parameter(Mandatory=$true)][string]$ExpectedSHA256, [Parameter(Mandatory=$true)][ValidatePattern('^[0-9A-Fa-f]{40}$')][string]$CertificateThumbprint)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$source=Join-Path $projectRoot 'build\driver\Release\DarkEdenReader.sys'
if ($ExpectedSHA256 -notmatch '^[0-9A-Fa-f]{64}$') { throw 'Invalid expected SHA256.' }
$destination=Join-Path 'C:\ProgramData\DarkEdenMemoryReader' ('DarkEdenReader-'+$ExpectedSHA256.Substring(0,12)+'.sys')
$result=[ordered]@{Time=(Get-Date).ToString('o');StartCode=$null;Success=$false;Error=$null;InitializationStage=$null;InitializationStatus=$null;State=$null}
try {
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $ExpectedSHA256) { throw 'Source hash mismatch.' }
    $signature=Get-AuthenticodeSignature -LiteralPath $source
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $CertificateThumbprint) { throw 'Signature mismatch.' }
    if ((Get-CimInstance Win32_SystemDriver -Filter "Name='DarkEdenReader'").State -ne 'Stopped') { throw 'Driver must be stopped before replacement.' }
    Copy-Item -LiteralPath $source -Destination $destination -Force
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $ExpectedSHA256) { throw 'Destination hash mismatch.' }
    $configOutput=& sc.exe config DarkEdenReader binPath= $destination 2>&1
    if ($LASTEXITCODE -ne 0) { throw ($configOutput | Out-String) }
    # Clear previous attempt markers before a new load.
    Remove-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\DarkEdenReader' -Name InitializationStage,InitializationStatus -ErrorAction SilentlyContinue
    $startOutput=& sc.exe start DarkEdenReader 2>&1
    $startCode=$LASTEXITCODE
    $result.StartCode=$startCode
    $diagnostics=Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\DarkEdenReader'
    $result.InitializationStage=$diagnostics.InitializationStage
    if ($null -ne $diagnostics.InitializationStatus) { $result.InitializationStatus=('0x{0:X8}' -f $diagnostics.InitializationStatus) }
    $result.State=[string](Get-CimInstance Win32_SystemDriver -Filter "Name='DarkEdenReader'").State
    if ($startCode -ne 0) {
        throw ($startOutput | Out-String)
    }
    $result.Success=$true
} catch { $result.Error=$_.Exception.Message }
$result | ConvertTo-Json | Set-Content (Join-Path $projectRoot 'build\driver\Release\deploy-result.json') -Encoding UTF8
if (!$result.Success) { exit 1 }

