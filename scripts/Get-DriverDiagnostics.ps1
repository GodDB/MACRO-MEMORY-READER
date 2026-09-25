param([string]$OutputPath)
$ErrorActionPreference = 'Stop'
$report = [ordered]@{
    Time = (Get-Date).ToString('o')
    Service = $null
    Binary = $null
    CodeIntegrity = $null
    Initialization = $null
    GameProcessIds = @()
    XhunterState = $null
    Events = @()
    CollectionErrors = @()
}
try {
    $service = Get-CimInstance Win32_SystemDriver -Filter "Name='DarkEdenReader'"
    if ($service) {
        $report.Service = $service | Select-Object Name, State, StartMode, ExitCode, ServiceSpecificExitCode, PathName
        $path = [Environment]::ExpandEnvironmentVariables($service.PathName).Trim('"')
        if ($path.StartsWith('\??\')) { $path = $path.Substring(4) }
        if ($path.StartsWith('\SystemRoot\', [StringComparison]::OrdinalIgnoreCase)) {
            $path = Join-Path $env:SystemRoot $path.Substring(12)
        }
        if (Test-Path -LiteralPath $path) {
            $signature = Get-AuthenticodeSignature -LiteralPath $path
            $report.Binary = [ordered]@{
                Path = $path
                SHA256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
                SignatureStatus = [string]$signature.Status
                SignerThumbprint = $signature.SignerCertificate.Thumbprint
            }
        }
        $key = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\DarkEdenReader'
        $report.Initialization = [ordered]@{Stage=$key.InitializationStage; Status=$key.InitializationStatus}
    }
} catch { $report.CollectionErrors += 'Service: ' + $_.Exception.Message }
try {
    if (!('DarkEdenDiagnostics.CodeIntegrity' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace DarkEdenDiagnostics {
 public static class CodeIntegrity {
  [StructLayout(LayoutKind.Sequential)] public struct Info { public uint Length; public uint Options; }
  [DllImport("ntdll.dll")] public static extern int NtQuerySystemInformation(int type, ref Info info, int size, out int returned);
 }
}
'@
    }
    $info = New-Object DarkEdenDiagnostics.CodeIntegrity+Info
    $info.Length = 8; $returned = 0
    $status = [DarkEdenDiagnostics.CodeIntegrity]::NtQuerySystemInformation(103, [ref]$info, 8, [ref]$returned)
    if ($status -ne 0) { throw ('NtQuerySystemInformation: 0x{0:X8}' -f $status) }
    $report.CodeIntegrity = [ordered]@{
        Options = ('0x{0:X8}' -f $info.Options)
        TestSigningActive = (($info.Options -band 2) -ne 0)
    }
} catch { $report.CollectionErrors += 'CodeIntegrity: ' + $_.Exception.Message }
try {
    $report.GameProcessIds = @(Get-Process darkeden -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
    $otherDriver = Get-CimInstance Win32_SystemDriver -Filter "Name='xhunter1'"
    if ($otherDriver) { $report.XhunterState = $otherDriver.State }
    # Presence is environment evidence only; it does not prove a conflict.
} catch { $report.CollectionErrors += 'Environment: ' + $_.Exception.Message }
foreach ($log in @('System', 'Microsoft-Windows-CodeIntegrity/Operational')) {
    try {
        $events = Get-WinEvent -FilterHashtable @{LogName=$log; StartTime=(Get-Date).AddMinutes(-30)} -MaxEvents 300 -ErrorAction Stop
        $report.Events += @($events | Where-Object { $_.Message -match 'DarkEdenReader|DarkEdenLoadProbe' } |
            Select-Object TimeCreated, Id, ProviderName, Message)
    } catch {
        if ($_.FullyQualifiedErrorId -notlike 'NoMatchingEventsFound*') {
            $report.CollectionErrors += $log + ': ' + $_.Exception.Message
        }
    }
}
if ($OutputPath) { $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding UTF8 }
[pscustomobject]$report
