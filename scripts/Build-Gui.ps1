param([string]$Compiler = 'g++', [switch]$RunTests)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force build\gui, build\tests | Out-Null
    & $Compiler -std=c++17 -O2 -Wall -Wextra -Werror -municode -mwindows -static -DUNICODE -D_UNICODE gui\Main.cpp -o build\gui\DarkEdenReaderGui.exe -luser32 -lgdi32 -lcomctl32 -ladvapi32
    if ($LASTEXITCODE -ne 0) { throw 'GUI build failed.' }
    if ($RunTests) {
        & $Compiler -std=c++17 -Wall -Wextra -Werror -static -Itests/stubs -I. tests\CoordinateReaderTests.cpp -o build\tests\CoordinateReaderTests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Coordinate tests build failed.' }
        & .\build\tests\CoordinateReaderTests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Coordinate tests failed.' }
        & $Compiler -std=c++17 -Wall -Wextra -Werror -static -DUNICODE -D_UNICODE tests\DriverClientTests.cpp -o build\tests\DriverClientTests.exe -ladvapi32
        if ($LASTEXITCODE -ne 0) { throw 'Client tests build failed.' }
        & .\build\tests\DriverClientTests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Client tests failed.' }
    }
    Write-Output "GUI: $projectRoot\build\gui\DarkEdenReaderGui.exe"
} finally { Pop-Location }
