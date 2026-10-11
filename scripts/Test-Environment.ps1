#Requires -RunAsAdministrator
param([switch]$OpenGui)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$log = Join-Path $projectRoot 'build\environment-verification.log'
Start-Transcript -LiteralPath $log -Force | Out-Null
try {
    $report = & (Join-Path $PSScriptRoot 'Get-DriverDiagnostics.ps1')
    if (!$report.CodeIntegrity.TestSigningActive) {
        throw 'Test signing is not active. Restart Windows after setup, then run this script again.'
    }
    $service = Get-CimInstance Win32_SystemDriver -Filter "Name='DarkEdenReader'"
    if (!$service) { throw 'DarkEdenReader driver service is not installed.' }
    if ($service.State -ne 'Running') {
        & sc.exe start DarkEdenReader
        if ($LASTEXITCODE -ne 0) { throw 'Driver start failed. See build/environment-verification.log.' }
    }
    & (Join-Path $projectRoot 'build\tests\fixture\darkeden.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Self-owned memory integration test failed.' }
    $games = @(Get-Process darkeden -ErrorAction SilentlyContinue)
    foreach ($game in $games) {
        & (Join-Path $projectRoot 'build\tests\ReadDirectValue.exe') $game.Id --xy
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Game PID $($game.Id): read failed. The error above is separate from the passing fixture test."
        }
    }
    if ($games.Count -eq 0) { Write-Output 'No game process found. Start the game and use the GUI.' }
    if ($OpenGui) { Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'Run-Gui.ps1')) -WindowStyle Hidden }
} finally {
    & (Join-Path $PSScriptRoot 'Get-DriverDiagnostics.ps1') -OutputPath (Join-Path $projectRoot 'build\environment-diagnostics.json') | Out-Null
    Stop-Transcript | Out-Null
}
