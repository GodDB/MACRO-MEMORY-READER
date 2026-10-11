param([string]$PythonPath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (!$PythonPath) {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313\python.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'),
        (Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe')
    )
    $PythonPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (!$PythonPath) {
    $command = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($command -and $command.Source -notlike '*\WindowsApps\*') { $PythonPath = $command.Source }
}
if (!$PythonPath) { throw 'Python 3 with tkinter was not found. Pass -PythonPath.' }
& $PythonPath (Join-Path $projectRoot 'python\gui.py')
if ($LASTEXITCODE -ne 0) { throw "Python GUI exited with code $LASTEXITCODE" }
