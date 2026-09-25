# Run only after building and signing the driver for this Windows installation.
# This script does not change Windows signing, Secure Boot, or security settings.
#Requires -RunAsAdministrator
param([Parameter(Mandatory=$true)][string]$DriverPath)
$ErrorActionPreference = 'Stop'
$resolvedDriver = (Resolve-Path -LiteralPath $DriverPath).Path
if ([IO.Path]::GetExtension($resolvedDriver) -ne '.sys') { throw 'Expected a .sys file.' }
$signature = Get-AuthenticodeSignature -LiteralPath $resolvedDriver
if ($signature.Status -ne 'Valid') {
    throw "A valid embedded driver signature is required. Signature status: $($signature.Status)"
}
$serviceName = 'DarkEdenReader'
if (Get-Service -Name $serviceName -ErrorAction SilentlyContinue) {
    throw 'DarkEdenReader already exists. Use Uninstall-Driver.ps1 before installing a different build.'
}
# Keep a stable service binary independent of the source/OneDrive directory.
$destination = Join-Path $env:ProgramData 'DarkEdenMemoryReader'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$serviceBinary = Join-Path $destination 'DarkEdenReader.sys'
Copy-Item -LiteralPath $resolvedDriver -Destination $serviceBinary -Force
& sc.exe create $serviceName type= kernel start= demand binPath= ('"' + $serviceBinary + '"')
if ($LASTEXITCODE -ne 0) { throw 'Service creation failed.' }
& sc.exe start $serviceName
if ($LASTEXITCODE -ne 0) { throw 'Driver start failed. The service remains installed; inspect the sc.exe error and Windows driver-signing requirements.' }
Write-Output 'DarkEdenReader driver started.'
