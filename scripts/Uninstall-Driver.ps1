#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
$service = Get-Service -Name DarkEdenReader -ErrorAction SilentlyContinue
if (!$service) { Write-Output 'DarkEdenReader is not installed.'; return }
if ($service.Status -ne 'Stopped') {
    & sc.exe stop DarkEdenReader
    if ($LASTEXITCODE -ne 0) { throw 'Could not stop the driver. Close all reader GUIs and retry.' }
    $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(15))
}
& sc.exe delete DarkEdenReader
if ($LASTEXITCODE -ne 0) { throw 'Could not remove the driver service.' }
Write-Output 'DarkEdenReader service removed. The .sys file is retained in ProgramData.'
