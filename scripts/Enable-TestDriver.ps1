#Requires -RunAsAdministrator
# Explicitly authorized development-machine setup. Does not reboot Windows.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$source = Join-Path $projectRoot 'build\driver\Release\DarkEdenReader.sys'
$certificatePath = Join-Path $projectRoot 'build\driver\Release\signing\DarkEdenReader-Test.cer'
$resultPath = Join-Path $projectRoot 'build\driver\Release\test-setup-result.json'
$logPath = Join-Path $projectRoot 'build\driver\Release\test-setup.log'
$expectedHash = '883014B94C0E0E033F2BF2984D74CF1FB16788FB3175DD3A5BC5F086A80AA079'
$expectedCertificateHash = 'DCC7A7BF98079F8CC80ACEC48BD398296DCB4B7E0734309E074108B7C8D298E7'
$thumbprint = '313CA2F1B0EE2F9F8D96996B66B37058D2495BD3'
$destination = Join-Path $env:ProgramData 'DarkEdenMemoryReader'
$serviceBinary = Join-Path $destination 'DarkEdenReader.sys'
$result = [ordered]@{
    Started=(Get-Date).ToString('o'); Success=$false; DriverSHA256=$expectedHash
    CertificateThumbprint=$thumbprint; RootTrusted=$false; PublisherTrusted=$false
    SignatureStatus=$null; TestSigningConfigured=$false; TestSigningActive=$false
    ServiceRegistered=$false; ServiceState=$null; RebootRequired=$false
    BinaryPath=$serviceBinary; Error=$null
}
Start-Transcript -LiteralPath $logPath -Force | Out-Null
try {
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $expectedHash) {
        throw 'Driver differs from the reviewed, signed build. No setup changes made.'
    }
    if ((Get-FileHash -LiteralPath $certificatePath -Algorithm SHA256).Hash -ne $expectedCertificateHash) {
        throw 'Certificate differs from the reviewed certificate. No setup changes made.'
    }
    $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($certificatePath)
    if ($certificate.Thumbprint -ne $thumbprint) { throw 'Unexpected certificate.' }
    $signature = Get-AuthenticodeSignature -LiteralPath $source
    if (!$signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ne $thumbprint) {
        throw 'Driver does not have the expected embedded signature.'
    }
    $existing = Get-CimInstance Win32_SystemDriver -Filter "Name='DarkEdenReader'" -ErrorAction Stop
    if ($existing -and $existing.PathName.Trim('"') -ne $serviceBinary) {
        throw "An existing DarkEdenReader service refers to a different binary: $($existing.PathName)"
    }
    if ($existing -and $existing.State -ne 'Stopped') {
        throw 'Close the GUI and stop the existing driver before configuring this build.'
    }
    & "$env:SystemRoot\System32\bcdedit.exe" /enum '{current}'
    if ($LASTEXITCODE -ne 0) { throw 'Could not read the current boot configuration.' }

    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $serviceBinary -Force
    Copy-Item -LiteralPath $certificatePath -Destination (Join-Path $destination 'DarkEdenReader-Test.cer') -Force
    if ((Get-FileHash -LiteralPath $serviceBinary -Algorithm SHA256).Hash -ne $expectedHash) {
        throw 'Copied driver hash mismatch.'
    }
    foreach ($store in @('Root','TrustedPublisher')) {
        if (!(Test-Path -LiteralPath "Cert:\LocalMachine\$store\$thumbprint")) {
            Import-Certificate -FilePath $certificatePath -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
        }
    }
    $result.RootTrusted = Test-Path -LiteralPath "Cert:\LocalMachine\Root\$thumbprint"
    $result.PublisherTrusted = Test-Path -LiteralPath "Cert:\LocalMachine\TrustedPublisher\$thumbprint"
    $signature = Get-AuthenticodeSignature -LiteralPath $serviceBinary
    $result.SignatureStatus = [string]$signature.Status
    if ($signature.Status -ne 'Valid') { throw "Windows signature verification failed: $($signature.StatusMessage)" }

    # Change only TESTSIGNING on the current Windows boot entry.
    & "$env:SystemRoot\System32\bcdedit.exe" /set '{current}' testsigning on
    if ($LASTEXITCODE -ne 0) { throw 'TESTSIGNING could not be enabled. See the BCDEdit error in the log.' }
    $result.TestSigningConfigured = $true
    & "$env:SystemRoot\System32\bcdedit.exe" /enum '{current}'

    if (!$existing) {
        & "$env:SystemRoot\System32\sc.exe" create DarkEdenReader type= kernel start= demand binPath= $serviceBinary
        if ($LASTEXITCODE -ne 0) { throw 'Driver service registration failed.' }
    }
    $result.ServiceRegistered = $true
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class DarkEdenTestModeState {
    [StructLayout(LayoutKind.Sequential)] public struct Info { public UInt32 Length; public UInt32 Options; }
    [DllImport("ntdll.dll")] public static extern int NtQuerySystemInformation(int type, ref Info info, int size, out int returned);
}
'@
    $info = New-Object DarkEdenTestModeState+Info
    $info.Length = 8
    $returned = 0
    $status = [DarkEdenTestModeState]::NtQuerySystemInformation(103, [ref]$info, 8, [ref]$returned)
    if ($status -ne 0) { throw "Could not query active code integrity policy: $status" }
    $result.TestSigningActive = (($info.Options -band 2) -ne 0)
    $result.RebootRequired = !$result.TestSigningActive
    if ($result.TestSigningActive) {
        & "$env:SystemRoot\System32\sc.exe" start DarkEdenReader
        if ($LASTEXITCODE -ne 0) { throw 'Driver start failed. See the service error in the log.' }
    }
    $result.ServiceState = (Get-CimInstance Win32_SystemDriver -Filter "Name='DarkEdenReader'").State
    $result.Success = $true
    Write-Output ('Setup complete. Reboot required: ' + $result.RebootRequired)
}
catch {
    $result.Error = $_.Exception.Message
    Write-Output ('Setup stopped: ' + $result.Error)
}
finally {
    $result['Finished'] = (Get-Date).ToString('o')
    $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $resultPath -Encoding UTF8
    Stop-Transcript | Out-Null
}
if (!$result.Success) { exit 1 }
