#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$resultPath = Join-Path $projectRoot 'build\driver\Release\start-result.json'
$result = [ordered]@{Success=$false; ServiceState=$null; StartCode=$null; DeviceConnected=$false; Error=$null}
try {
    $configuration = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\DarkEdenReader'
    $binary = [Environment]::ExpandEnvironmentVariables($configuration.ImagePath).Trim('"')
    if ($binary.StartsWith('\??\')) { $binary = $binary.Substring(4) }
    $source = Join-Path $projectRoot 'build\driver\Release\DarkEdenReader.sys'
    if ((Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) { throw 'Installed driver differs from the current build. Deploy the signed build first.' }
    $signature = Get-AuthenticodeSignature -LiteralPath $binary
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne '313CA2F1B0EE2F9F8D96996B66B37058D2495BD3') { throw 'Driver signature is not trusted or the signer differs.' }
    $service = Get-Service DarkEdenReader
    if ($service.Status -ne 'Running') {
        $output = & sc.exe start DarkEdenReader 2>&1
        $result.StartCode = $LASTEXITCODE
        if ($result.StartCode -ne 0) { throw ($output | Out-String) }
    }
    $result.ServiceState = [string](Get-Service DarkEdenReader).Status
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class DarkEdenDeviceProbe {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 public static extern SafeFileHandle CreateFile(string name, uint access, uint sharing, IntPtr security, uint creation, uint attributes, IntPtr template);
}
'@
    # Windows PowerShell 5.1 treats the literal 0x80000000 as a negative Int32.
    $device = [DarkEdenDeviceProbe]::CreateFile('\\.\DarkEdenMemoryReader',[uint32]2147483648,3,[IntPtr]::Zero,3,0,[IntPtr]::Zero)
    if ($device.IsInvalid) { throw ('Device open failed: Win32 ' + [Runtime.InteropServices.Marshal]::GetLastWin32Error()) }
    $device.Dispose()
    $result.DeviceConnected = $true
    $result.Success = $true
} catch { $result.Error = $_.Exception.Message }
$result.ServiceState = [string](Get-Service DarkEdenReader -ErrorAction SilentlyContinue).Status
$result['Time'] = (Get-Date).ToString('o')
$result | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
if (!$result.Success) { exit 1 }

